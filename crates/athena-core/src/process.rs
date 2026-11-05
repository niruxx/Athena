//! Running package-manager commands: one at a time with a timeout, or in
//! sequence, with stable unlocalized output.

use std::io::{Read, Write};
use std::path::PathBuf;
use std::process::Stdio;
use std::thread;
use std::time::{Duration, Instant};

use crate::polkit;

/// One command line, as shown in the terminal window and run in order.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Command {
    pub program: String,
    pub args: Vec<String>,
    /// For tools that only write relative to their working directory
    /// (`apt-get download`, `snap download`).
    pub working_dir: Option<PathBuf>,
}

impl Command {
    pub fn new<I, S>(program: &str, args: I) -> Self
    where
        I: IntoIterator<Item = S>,
        S: Into<String>,
    {
        Self {
            program: program.to_owned(),
            args: args.into_iter().map(Into::into).collect(),
            working_dir: None,
        }
    }

    /// The same command through pkexec.
    pub fn privileged<I, S>(args: I) -> Self
    where
        I: IntoIterator<Item = S>,
        S: Into<String>,
    {
        Self::new("pkexec", args)
    }

    pub fn in_dir(mut self, dir: impl Into<PathBuf>) -> Self {
        self.working_dir = Some(dir.into());
        self
    }

    /// `program arg arg…`, for display.
    pub fn display(&self) -> String {
        std::iter::once(self.program.as_str())
            .chain(self.args.iter().map(String::as_str))
            .collect::<Vec<_>>()
            .join(" ")
    }

    /// The arguments to start it with (see [`launch_args`]).
    pub fn launch_args(&self) -> Vec<String> {
        launch_args(&self.program, &self.args)
    }
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Output {
    pub started: bool,
    pub exit_code: i32,
    pub stdout: String,
    pub stderr: String,
}

impl Output {
    pub fn success(&self) -> bool {
        self.started && self.exit_code == 0
    }

    /// stdout and stderr together, for an error message.
    pub fn combined(&self) -> String {
        format!("{}{}", self.stdout, self.stderr)
    }
}

pub const SHORT: Duration = Duration::from_secs(30);
pub const LONG: Duration = Duration::from_secs(10 * 60);
/// Updates and AUR builds can take a long time (big runtimes, slow mirrors).
pub const VERY_LONG: Duration = Duration::from_secs(2 * 60 * 60);

/// The arguments to start `program` with: for pkexec, with its text-mode
/// password prompt turned off. Without a polkit authentication agent, pkexec
/// would otherwise print a password prompt into the output and fail for want
/// of a terminal; this way it fails at once with "No authentication agent
/// found", which the UI explains.
pub fn launch_args(program: &str, args: &[String]) -> Vec<String> {
    let is_pkexec = program == "pkexec" || program.ends_with("/pkexec");
    if !is_pkexec || args.first().map(String::as_str) == Some("--disable-internal-agent") {
        return args.to_vec();
    }
    std::iter::once("--disable-internal-agent".to_owned()).chain(args.iter().cloned()).collect()
}

/// Whether `program` resolves to something executable on PATH.
pub fn executable_exists(program: &str) -> bool {
    use std::os::unix::fs::PermissionsExt;
    let Some(path) = std::env::var_os("PATH") else { return false };
    std::env::split_paths(&path).any(|dir| {
        std::fs::metadata(dir.join(program))
            .map(|m| m.is_file() && m.permissions().mode() & 0o111 != 0)
            .unwrap_or(false)
    })
}

/// Builds the std command: unlocalized output (parsing depends on it) and
/// pkexec's text prompt turned off.
pub fn std_command(command: &Command) -> std::process::Command {
    if command.program == "pkexec" || command.program == "timeshift-launcher" {
        polkit::ensure_running(); // a password prompt needs an agent to show it
    }
    let mut cmd = std::process::Command::new(&command.program);
    cmd.args(command.launch_args()).env("LC_ALL", "C");
    if let Some(appdir)=std::env::var_os("APPDIR")
        && !std::path::Path::new(&command.program).starts_with(&appdir) {
            host_environment(&mut cmd);
        }
    if let Some(dir) = &command.working_dir {
        cmd.current_dir(dir);
    }
    cmd
}

/// AppImage GUI libraries must never be injected into native package tools.
fn host_environment(cmd:&mut std::process::Command) {
    for key in ["LD_LIBRARY_PATH","GSETTINGS_SCHEMA_DIR","GIO_MODULE_DIR","GIO_EXTRA_MODULES","GTK_PATH","GTK_EXE_PREFIX","GTK_DATA_PREFIX","GTK_IM_MODULE_FILE","GDK_PIXBUF_MODULE_FILE","GDK_PIXBUF_MODULEDIR","GI_TYPELIB_PATH","GTK_THEME"] {cmd.env_remove(key);}
    for (target,source) in [("PATH","ATHENA_HOST_PATH"),("LD_LIBRARY_PATH","ATHENA_HOST_LD_LIBRARY_PATH"),("XDG_DATA_DIRS","ATHENA_HOST_XDG_DATA_DIRS")] {
        if let Some(value)=std::env::var_os(source) { if value.is_empty(){cmd.env_remove(target);}else{cmd.env(target,value);} }
    }
}

fn run_command(command: &Command, stdin_data: Option<&[u8]>, timeout: Duration) -> Output {
    let mut cmd = std_command(command);
    cmd.stdin(if stdin_data.is_some() { Stdio::piped() } else { Stdio::null() })
        .stdout(Stdio::piped())
        .stderr(Stdio::piped());
    let Ok(mut child) = cmd.spawn() else { return Output::default() };

    if let (Some(data), Some(mut stdin)) = (stdin_data, child.stdin.take()) {
        let data = data.to_vec();
        thread::spawn(move || {
            let _ = stdin.write_all(&data);
        });
    }
    // Read both pipes on their own threads so a chatty process can't block
    // on a full pipe while we wait for it.
    let mut out_pipe = child.stdout.take();
    let mut err_pipe = child.stderr.take();
    let out_reader = thread::spawn(move || {
        let mut buf = Vec::new();
        if let Some(pipe) = out_pipe.as_mut() {
            let _ = pipe.read_to_end(&mut buf);
        }
        buf
    });
    let err_reader = thread::spawn(move || {
        let mut buf = Vec::new();
        if let Some(pipe) = err_pipe.as_mut() {
            let _ = pipe.read_to_end(&mut buf);
        }
        buf
    });

    let deadline = Instant::now() + timeout;
    let status = loop {
        match child.try_wait() {
            Ok(Some(status)) => break Some(status),
            Ok(None) if Instant::now() >= deadline => {
                let _ = child.kill();
                break child.wait().ok();
            }
            Ok(None) => thread::sleep(Duration::from_millis(15)),
            Err(_) => break None,
        }
    };
    let stdout = String::from_utf8_lossy(&out_reader.join().unwrap_or_default()).into_owned();
    let stderr = String::from_utf8_lossy(&err_reader.join().unwrap_or_default()).into_owned();
    Output {
        started: true,
        exit_code: status.and_then(|s| s.code()).unwrap_or(-1),
        stdout,
        stderr,
    }
}

/// Runs `program` and waits for it (killed after `timeout`).
pub fn run<S: AsRef<str>>(program: &str, args: &[S], timeout: Duration) -> Output {
    run_command(&Command::new(program, args.iter().map(|a| a.as_ref().to_owned())), None, timeout)
}

/// Runs `program` with `stdin_data` on its standard input.
pub fn run_with_stdin<S: AsRef<str>>(program: &str, args: &[S], stdin_data: &[u8], timeout: Duration) -> Output {
    run_command(&Command::new(program, args.iter().map(|a| a.as_ref().to_owned())), Some(stdin_data), timeout)
}

/// Runs each command in order, stopping at the first failure; the output
/// of every command that ran, concatenated.
pub fn run_sequence(commands: &[Command], timeout_per_command: Duration) -> Output {
    let mut all = Output {
        started: true,
        ..Default::default()
    };
    for command in commands {
        let output = run_command(command, None, timeout_per_command);
        all.stdout.push_str(&output.stdout);
        all.stderr.push_str(&output.stderr);
        all.exit_code = output.exit_code;
        if !output.success() {
            all.started = output.started;
            return all;
        }
    }
    all
}

/// Splits on line breaks, dropping empty lines.
pub(crate) fn lines(text: &str) -> impl Iterator<Item = &str> {
    text.lines().filter(|l| !l.trim().is_empty())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn pkexec_gets_its_text_prompt_turned_off_once() {
        let args = vec!["pacman".to_owned(), "-Syu".to_owned()];
        assert_eq!(launch_args("pkexec", &args), ["--disable-internal-agent", "pacman", "-Syu"]);
        let already = launch_args("pkexec", &args);
        assert_eq!(launch_args("pkexec", &already), already);
        assert_eq!(launch_args("flatpak", &args), args);
    }

    #[test]
    fn runs_and_times_out() {
        let ok = run("sh", &["-c", "echo hi; echo err >&2"], SHORT);
        assert!(ok.success());
        assert_eq!(ok.stdout, "hi\n");
        assert_eq!(ok.stderr, "err\n");
        let slow = run("sh", &["-c", "sleep 5"], Duration::from_millis(100));
        assert!(!slow.success());
        assert!(!run("athena-no-such-program", &["x"], SHORT).started);
    }

    #[test]
    fn sequence_stops_at_first_failure() {
        let commands = [
            Command::new("sh", ["-c", "echo one"]),
            Command::new("sh", ["-c", "exit 3"]),
            Command::new("sh", ["-c", "echo three"]),
        ];
        let out = run_sequence(&commands, SHORT);
        assert_eq!(out.exit_code, 3);
        assert_eq!(out.stdout, "one\n");
    }

    #[test]
    fn stdin_is_passed_through() {
        assert_eq!(run_with_stdin("cat", &[] as &[&str], b"data", SHORT).stdout, "data");
    }
}

#[cfg(test)]
mod bundle_tests {
    #[test]
    fn host_commands_do_not_inherit_bundled_modules() {
        let mut command=std::process::Command::new("true");
        for key in ["GSETTINGS_SCHEMA_DIR","GTK_PATH","GIO_MODULE_DIR","GDK_PIXBUF_MODULE_FILE"]{command.env(key,"/bundle/resources");}
        super::host_environment(&mut command);
        for key in ["GSETTINGS_SCHEMA_DIR","GTK_PATH","GIO_MODULE_DIR","GDK_PIXBUF_MODULE_FILE"]{assert!(command.get_envs().any(|(name,value)|name==key&&value.is_none()));}
    }
}
