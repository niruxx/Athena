//! AUR RPC v5 and a fail-closed, reviewed local build pipeline.
//! Fetching and dependency planning never source PKGBUILD or invoke makepkg.
use crate::{Backend, Command, PackageInfo, backends::PacmanBackend, process};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::{
    collections::{BTreeMap, HashMap, HashSet},
    fs,
    path::{Component, Path, PathBuf},
    time::{SystemTime, UNIX_EPOCH},
};

#[derive(Clone, Debug, Default, Deserialize)]
#[serde(default)]
pub struct Package {
    #[serde(rename = "Name")]
    pub name: String,
    #[serde(rename = "PackageBase")]
    pub base: String,
    #[serde(rename = "Version")]
    pub version: String,
    #[serde(rename = "Description")]
    pub description: Option<String>,
    #[serde(rename = "URL")]
    pub url: Option<String>,
    #[serde(rename = "Depends")]
    pub depends: Vec<String>,
    #[serde(rename = "MakeDepends")]
    pub make_depends: Vec<String>,
    #[serde(rename = "CheckDepends")]
    pub check_depends: Vec<String>,
    #[serde(rename = "Provides")]
    pub provides: Vec<String>,
}
pub struct AurBackend;
pub fn valid_name(name: &str) -> bool {
    !name.is_empty() && !name.starts_with(['-', '.']) && name.bytes().all(|b| b.is_ascii_alphanumeric() || b"@._+-".contains(&b))
}
fn encode(value: &str) -> String {
    value
        .bytes()
        .map(|b| {
            if b.is_ascii_alphanumeric() || b"-._~".contains(&b) {
                (b as char).to_string()
            } else {
                format!("%{b:02X}")
            }
        })
        .collect()
}
pub fn parse_rpc(text: &str) -> Result<Vec<Package>, String> {
    let value: serde_json::Value = serde_json::from_str(text).map_err(|e| e.to_string())?;
    if value["type"] == "error" {
        return Err(value["error"].as_str().unwrap_or("AUR RPC error").into());
    }
    let packages: Vec<Package> = serde_json::from_value(value["results"].clone()).map_err(|e| e.to_string())?;
    if packages.iter().any(|p| !valid_name(&p.name) || !valid_name(&p.base) || p.version.is_empty()) {
        return Err("Malformed package in AUR response".into());
    }
    Ok(packages)
}
fn rpc(url: &str) -> Result<Vec<Package>, String> {
    let out = process::run(
        "curl",
        &[
            "--fail",
            "--silent",
            "--show-error",
            "--location",
            "--proto",
            "=https",
            "--proto-redir",
            "=https",
            "--max-time",
            "20",
            "--max-filesize",
            "16777216",
            url,
        ],
        std::time::Duration::from_secs(25),
    );
    if out.success() { parse_rpc(&out.stdout) } else { Err(out.combined()) }
}
pub fn info(names: &[String]) -> Result<Vec<Package>, String> {
    let mut result = Vec::new();
    for names in names.chunks(100) {
        if names.iter().any(|n| !valid_name(n)) {
            return Err("Invalid AUR package name".into());
        }
        let query = names.iter().map(|n| format!("arg%5B%5D={}", encode(n))).collect::<Vec<_>>().join("&");
        result.extend(rpc(&format!("https://aur.archlinux.org/rpc/v5/info?{query}"))?);
    }
    Ok(result)
}
pub fn foreign() -> HashMap<String, String> {
    process::run("pacman", &["-Qm"], process::SHORT)
        .stdout
        .lines()
        .filter_map(|line| line.split_once(' ').map(|(n, v)| (n.to_owned(), v.trim().to_owned())))
        .collect()
}
fn package_info(p: Package, installed: &HashMap<String, String>) -> PackageInfo {
    PackageInfo {
        name: p.name.clone(),
        installed: installed.contains_key(&p.name),
        installed_version: installed.get(&p.name).cloned().unwrap_or_default(),
        available_version: p.version,
        description: p.description.unwrap_or_default(),
        homepage_url: p.url.unwrap_or_default(),
        repository: "AUR".into(),
        ..Default::default()
    }
}
impl Backend for AurBackend {
    fn key(&self) -> &'static str {
        "aur"
    }
    fn name(&self) -> &'static str {
        "Arch User Repository"
    }
    fn is_available(&self) -> bool {
        process::executable_exists("pacman")
    }
    fn search(&self, query: &str) -> Vec<PackageInfo> {
        if query.chars().count() < 2 {
            return Vec::new();
        }
        let installed = foreign();
        rpc(&format!("https://aur.archlinux.org/rpc/v5/search/{}?by=name-desc", encode(query)))
            .unwrap_or_default()
            .into_iter()
            .map(|p| package_info(p, &installed))
            .collect()
    }
    fn package_details(&self, names: &[String]) -> Vec<PackageInfo> {
        let installed = foreign();
        info(names).unwrap_or_default().into_iter().map(|p| package_info(p, &installed)).collect()
    }
    fn list_installed(&self) -> Vec<PackageInfo> {
        let installed = foreign();
        info(&installed.keys().cloned().collect::<Vec<_>>())
            .unwrap_or_default()
            .into_iter()
            .map(|p| package_info(p, &installed))
            .collect()
    }
    fn list_updates(&self) -> Vec<PackageInfo> {
        self.list_installed()
            .into_iter()
            .filter(|p| {
                process::run("vercmp", &[&p.available_version, &p.installed_version], process::SHORT)
                    .stdout
                    .trim()
                    .parse::<i32>()
                    .is_ok_and(|n| n > 0)
            })
            .collect()
    }
    // A generic transaction must never bypass review. The UI invokes prepare/approve
    // and the manifest runner instead. No helper may refetch a different PKGBUILD.
    fn install_commands(&self, _: &[String]) -> Vec<Command> {
        Vec::new()
    }
    fn reinstall_commands(&self, _: &[String]) -> Vec<Command> {
        Vec::new()
    }
    fn upgrade_commands(&self, _: &[String]) -> Vec<Command> {
        Vec::new()
    }
    fn remove_commands(&self, names: &[String]) -> Vec<Command> {
        PacmanBackend.remove_commands(names)
    }
    fn clean_unused_commands(&self) -> Vec<Command> {
        Vec::new()
    }
    fn download_commands(&self, names: &[String], destination: &str, _: bool) -> Vec<Command> {
        names
            .iter()
            .filter(|n| valid_name(n))
            .map(|n| {
                Command::new(
                    "git",
                    [
                        "-c",
                        "core.hooksPath=/dev/null",
                        "clone",
                        "--depth=1",
                        "--",
                        &format!("https://aur.archlinux.org/{n}.git"),
                        &Path::new(destination).join(n).to_string_lossy(),
                    ],
                )
            })
            .collect()
    }
    fn recent_history(&self) -> String {
        PacmanBackend.recent_history()
    }
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct BuildFile {
    pub name: String,
    pub text: String,
    pub previous: Option<String>,
    pub digest: String,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Unit {
    pub base: String,
    pub packages: Vec<String>,
    pub as_dependencies: bool,
    #[serde(default)]
    pub dependency_packages: Vec<String>,
    pub files: Vec<BuildFile>,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Plan {
    pub root: PathBuf,
    pub units: Vec<Unit>,
}
fn digest(bytes: &[u8]) -> String {
    format!("{:x}", Sha256::digest(bytes))
}
fn user_directory(variable: &str, fallback: &str) -> Result<PathBuf, String> {
    let path = std::env::var_os(variable)
        .map(PathBuf::from)
        .or_else(|| std::env::var_os("HOME").map(|h| PathBuf::from(h).join(fallback)))
        .ok_or("No user home directory")?;
    if !path.is_absolute() {
        return Err("User directory must be absolute".into());
    }
    Ok(path.join("athena/aur"))
}
pub fn state_root() -> Result<PathBuf, String> {
    user_directory("XDG_STATE_HOME", ".local/state")
}
pub fn cache_root() -> Result<PathBuf, String> {
    user_directory("XDG_CACHE_HOME", ".cache")
}
fn safe_file(name: &str) -> bool {
    !name.is_empty()
        && !Path::new(name).is_absolute()
        && !name.chars().any(char::is_control)
        && Path::new(name).components().all(|c| matches!(c,Component::Normal(v) if v!=".git"))
}
fn read_tree(root: &Path, path: &Path, files: &mut Vec<BuildFile>, total: &mut usize) -> Result<(), String> {
    for entry in fs::read_dir(path).map_err(|e| e.to_string())? {
        let entry = entry.map_err(|e| e.to_string())?;
        if entry.file_name() == ".git" {
            return Err("Git metadata appeared in a reviewed build directory".into());
        }
        let path = entry.path();
        let metadata = fs::symlink_metadata(&path).map_err(|e| e.to_string())?;
        if metadata.file_type().is_symlink() {
            return Err(format!("Cannot safely review symlink {}", path.display()));
        }
        if metadata.is_dir() {
            read_tree(root, &path, files, total)?;
        } else if metadata.is_file() {
            let name = path.strip_prefix(root).map_err(|e| e.to_string())?.to_string_lossy().into_owned();
            if !safe_file(&name) {
                return Err("Unsupported build filename".into());
            }
            if metadata.len() > 4 * 1024 * 1024 || files.len() >= 512 {
                return Err("Build files exceed review limits".into());
            }
            let bytes = fs::read(&path).map_err(|e| e.to_string())?;
            *total += bytes.len();
            if *total > 16 * 1024 * 1024 {
                return Err("Build files exceed review limits".into());
            }
            let text = String::from_utf8(bytes).map_err(|_| format!("Cannot review binary file {name}"))?;
            if text.contains('\0') {
                return Err(format!("Cannot review binary file {name}"));
            }
            files.push(BuildFile {
                name,
                text,
                previous: None,
                digest: String::new(),
            });
        }
    }
    Ok(())
}
fn fetch(base: &str, root: &Path) -> Result<Vec<BuildFile>, String> {
    fetch_url(base, root, &format!("https://aur.archlinux.org/{base}.git"))
}
fn fetch_url(base: &str, root: &Path, url: &str) -> Result<Vec<BuildFile>, String> {
    if !valid_name(base) {
        return Err("Invalid AUR package base".into());
    }
    let path = root.join(base);
    let command = Command::new(
        "env",
        [
            "GIT_CONFIG_GLOBAL=/dev/null",
            "GIT_CONFIG_NOSYSTEM=1",
            "git",
            "-c",
            "core.hooksPath=/dev/null",
            "-c",
            "protocol.file.allow=never",
            "clone",
            "--depth=1",
            "--",
            url,
            &path.to_string_lossy(),
        ],
    );
    let out = process::run_sequence(&[command], process::LONG);
    if !out.success() {
        return Err(out.combined());
    }
    let tree = process::run_sequence(
        &[Command::new(
            "env",
            [
                "GIT_CONFIG_GLOBAL=/dev/null",
                "GIT_CONFIG_NOSYSTEM=1",
                "git",
                "-c",
                "core.hooksPath=/dev/null",
                "ls-files",
                "--stage",
            ],
        )
        .in_dir(&path)],
        process::SHORT,
    );
    if !tree.success() || tree.stdout.lines().any(|l| !l.starts_with("100644 ") && !l.starts_with("100755 ")) {
        return Err("Submodules and symlinks are not supported in reviewed builds".into());
    }
    fs::remove_dir_all(path.join(".git")).map_err(|e| e.to_string())?;
    let mut files = Vec::new();
    read_tree(&path, &path, &mut files, &mut 0)?;
    if !files.iter().any(|f| f.name == "PKGBUILD") {
        return Err("The repository has no PKGBUILD".into());
    }
    // Never build in a mutable git checkout. Every displayed file is copied to a
    // fresh, private directory; no later fetch or helper replaces the review.
    let prior = state_root()?.join("approved").join(base).join("latest.json");
    if let Ok(previous) = fs::read_to_string(prior).and_then(|s| serde_json::from_str::<Unit>(&s).map_err(std::io::Error::other)) {
        for file in &mut files {
            file.previous = previous.files.iter().find(|f| f.name == file.name).map(|f| f.text.clone());
        }
    }
    files.sort_by_key(|f| if f.name == "PKGBUILD" { String::new() } else { f.name.clone() });
    Ok(files)
}
fn dependency_name(value: &str) -> &str {
    value.split(['<', '>', '=']).next().unwrap_or(value)
}
fn installed_or_native(dependency: &str) -> bool {
    let installed = process::run("pacman", &["-T", "--", dependency], process::SHORT);
    if installed.success() {
        return true;
    }
    process::run("pacman", &["-Sp", "--print-format", "%n", "--", dependency], process::SHORT).success()
}
fn srcinfo_dependencies(files: &[BuildFile], architecture: &str) -> Vec<String> {
    files
        .iter()
        .filter(|f| f.name == ".SRCINFO")
        .flat_map(|f| f.text.lines())
        .filter_map(|line| {
            let (key, value) = line.trim().split_once(" = ")?;
            let (key, arch) = key.split_once('_').map(|(key, arch)| (key, Some(arch))).unwrap_or((key, None));
            if arch.is_some_and(|arch| arch != architecture) {
                return None;
            }
            if matches!(key, "depends" | "makedepends" | "checkdepends") {
                Some(value.trim().to_owned())
            } else {
                None
            }
        })
        .collect()
}
struct Resolver {
    architecture: String,
    root: PathBuf,
    visiting: HashSet<String>,
    done: HashSet<String>,
    units: Vec<Unit>,
    roots: HashSet<String>,
    total_bytes: usize,
    total_files: usize,
}
impl Resolver {
    fn visit(&mut self, dependency: &str) -> Result<(), String> {
        let name = dependency_name(dependency);
        if self.units.len() + self.visiting.len() >= 128 {
            return Err("AUR dependency graph exceeds review limits".into());
        }
        let mut candidates = info(&[name.to_owned()])?;
        if candidates.is_empty() {
            let providers = rpc(&format!("https://aur.archlinux.org/rpc/v5/search/{}?by=provides", encode(name)))?;
            candidates = info(&providers.into_iter().map(|p| p.name).collect::<Vec<_>>())?
                .into_iter()
                .filter(|p| p.provides.iter().any(|cap| dependency_name(cap) == name))
                .collect();
        }
        if candidates.len() != 1 {
            return Err(format!(
                "Cannot resolve a unique AUR provider for {dependency}. Install a provider first and retry."
            ));
        }
        let package = candidates.pop().unwrap();
        let constraint = &dependency[name.len()..];
        if !constraint.is_empty() {
            let operator = constraint.trim_end_matches(|c: char| !matches!(c, '<' | '>' | '='));
            let required = &constraint[operator.len()..];
            let comparison = process::run(
                "vercmp",
                &[
                    if package.name == name {
                        package.version.as_str()
                    } else {
                        package
                            .provides
                            .iter()
                            .find(|cap| dependency_name(cap) == name)
                            .and_then(|cap| cap.split_once('=').map(|(_, v)| v))
                            .ok_or("AUR provider has no version for this capability")?
                    },
                    required,
                ],
                process::SHORT,
            )
            .stdout
            .trim()
            .parse::<i32>()
            .map_err(|_| "Cannot compare AUR dependency versions")?;
            let satisfies = match operator {
                ">=" => comparison >= 0,
                "<=" => comparison <= 0,
                ">" => comparison > 0,
                "<" => comparison < 0,
                "=" => comparison == 0,
                _ => false,
            };
            if !satisfies {
                return Err(format!("AUR version {} cannot satisfy {dependency}", package.version));
            }
        }
        if self.done.contains(&package.base) {
            if let Some(unit) = self.units.iter_mut().find(|u| u.base == package.base)
                && !unit.packages.contains(&package.name) {
                    unit.packages.push(package.name);
                }
            return Ok(());
        }
        if !self.visiting.insert(package.base.clone()) {
            return Err(format!("AUR dependency cycle involving {}", package.base));
        }
        let files = fetch(&package.base, &self.root)?;
        self.total_bytes += files.iter().map(|f| f.text.len()).sum::<usize>();
        self.total_files += files.len();
        if self.total_bytes > 32 * 1024 * 1024 || self.total_files > 2048 {
            return Err("This transaction exceeds review size limits. Review fewer packages at a time.".into());
        }
        let mut dependencies = package.depends;
        dependencies.extend(package.make_depends);
        dependencies.extend(package.check_depends);
        dependencies.extend(srcinfo_dependencies(&files, &self.architecture));
        dependencies.sort();
        dependencies.dedup();
        let internal: HashSet<_> = files
            .iter()
            .filter(|f| f.name == ".SRCINFO")
            .flat_map(|f| f.text.lines())
            .filter_map(|line| line.trim().strip_prefix("pkgname = ").map(str::to_owned))
            .collect();
        let mut packages = vec![package.name.clone()];
        for dependency in dependencies {
            let name = dependency_name(&dependency);
            if internal.contains(name) {
                if !packages.iter().any(|p| p == name) {
                    packages.push(name.to_owned());
                }
                continue;
            }
            if !installed_or_native(&dependency) {
                if !valid_name(name) {
                    return Err(format!("Unsupported dependency {dependency}"));
                }
                self.visit(&dependency)?;
            }
        }
        self.visiting.remove(&package.base);
        self.done.insert(package.base.clone());
        self.units.push(Unit {
            base: package.base,
            packages,
            as_dependencies: !self.roots.contains(&package.name),
            dependency_packages: Vec::new(),
            files,
        });
        Ok(())
    }
}
pub fn prepare(names: &[String]) -> Result<Plan, String> {
    if names.is_empty() || names.iter().any(|n| !valid_name(n)) {
        return Err("Choose valid AUR packages".into());
    }
    let parent = cache_root()?.join("work");
    fs::create_dir_all(&parent).map_err(|e| e.to_string())?;
    let root = parent.join(format!(
        "{}-{}",
        std::process::id(),
        SystemTime::now().duration_since(UNIX_EPOCH).map_err(|e| e.to_string())?.as_nanos()
    ));
    fs::create_dir(&root).map_err(|e| e.to_string())?;
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        fs::set_permissions(&root, fs::Permissions::from_mode(0o700)).map_err(|e| e.to_string())?;
    }
    let mut resolver = Resolver {
        architecture: std::env::consts::ARCH.into(),
        root: root.clone(),
        visiting: HashSet::new(),
        done: HashSet::new(),
        units: Vec::new(),
        roots: names.iter().cloned().collect(),
        total_bytes: 0,
        total_files: 0,
    };
    for name in names {
        resolver.visit(name)?;
    }
    let explicit: HashSet<_> = process::run("pacman", &["-Qqe"], process::SHORT).stdout.lines().map(str::to_owned).collect();
    for unit in &mut resolver.units {
        unit.dependency_packages = unit
            .packages
            .iter()
            .filter(|p| !resolver.roots.contains(*p) && !explicit.contains(*p))
            .cloned()
            .collect();
        unit.as_dependencies = unit.dependency_packages.len() == unit.packages.len();
    }
    Ok(Plan { root, units: resolver.units })
}

pub fn risky_lines(text: &str) -> Vec<String> {
    text.lines()
        .enumerate()
        .filter(|(_, line)| {
            [
                "sudo ",
                "pkexec ",
                "curl ",
                "wget ",
                "eval ",
                "base64",
                "rm -rf",
                "/etc/",
                "systemctl",
                "chmod 777",
                "SKIP",
                "http://",
            ]
            .iter()
            .any(|word| line.contains(word))
        })
        .map(|(i, line)| format!("{}: {}", i + 1, line.trim()))
        .collect()
}
pub fn diff(previous: &str, current: &str) -> String {
    if previous == current {
        return "No changes from the last approved copy.".into();
    }
    similar::TextDiff::configure()
        .timeout(std::time::Duration::from_millis(500))
        .diff_lines(previous, current)
        .unified_diff()
        .context_radius(3)
        .header("Previously approved", "Current review")
        .to_string()
}

pub fn approve(plan: &mut Plan) -> Result<PathBuf, String> {
    validate_root(&plan.root)?;
    for unit in &mut plan.units {
        if !valid_name(&unit.base) || unit.packages.iter().any(|p| !valid_name(p)) {
            return Err("Invalid build unit".into());
        }
        for file in &mut unit.files {
            if !safe_file(&file.name) {
                return Err("Invalid build file".into());
            }
            let path = plan.root.join(&unit.base).join(&file.name);
            if fs::symlink_metadata(&path).map_err(|e| e.to_string())?.file_type().is_symlink() {
                return Err("Build file became a symlink".into());
            }
            fs::write(&path, &file.text).map_err(|e| e.to_string())?;
            file.digest = digest(file.text.as_bytes());
        }
        let approved = state_root()?.join("approved").join(&unit.base);
        fs::create_dir_all(&approved).map_err(|e| e.to_string())?;
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            fs::set_permissions(&approved, fs::Permissions::from_mode(0o700)).map_err(|e| e.to_string())?;
        }
        let json = serde_json::to_vec_pretty(&unit).map_err(|e| e.to_string())?;
        let hash = digest(&json);
        fs::write(approved.join(format!("{hash}.json")), &json).map_err(|e| e.to_string())?;
        fs::write(approved.join("latest.json"), json).map_err(|e| e.to_string())?;
    }
    let path = plan.root.join("transaction.json");
    fs::write(&path, serde_json::to_vec_pretty(plan).map_err(|e| e.to_string())?).map_err(|e| e.to_string())?;
    fs::write(plan.root.join("approved"), digest(&fs::read(&path).map_err(|e| e.to_string())?)).map_err(|e| e.to_string())?;
    Ok(path)
}
fn validate_root(root: &Path) -> Result<(), String> {
    let parent = cache_root()?.join("work");
    if root.parent() != Some(parent.as_path()) || fs::symlink_metadata(root).map_err(|e| e.to_string())?.file_type().is_symlink() {
        return Err("Invalid reviewed build directory".into());
    }
    Ok(())
}
pub fn verify_files(root: &Path, unit: &Unit) -> Result<(), String> {
    if !valid_name(&unit.base) || unit.files.is_empty() {
        return Err("Invalid build unit".into());
    }
    let directory = root.join(&unit.base);
    if fs::symlink_metadata(&directory).map_err(|e| e.to_string())?.file_type().is_symlink() {
        return Err("Build directory became a symlink".into());
    }
    let mut actual = Vec::new();
    read_tree(&directory, &directory, &mut actual, &mut 0)?;
    let expected: HashSet<_> = unit.files.iter().map(|f| f.name.as_str()).collect();
    let actual_names: HashSet<_> = actual.iter().map(|f| f.name.as_str()).collect();
    if expected != actual_names {
        return Err(format!("Build files for {} changed after review", unit.base));
    }
    for file in &unit.files {
        if !safe_file(&file.name)
            || file.digest != digest(file.text.as_bytes())
            || actual
                .iter()
                .find(|f| f.name == file.name)
                .is_none_or(|f| digest(f.text.as_bytes()) != file.digest)
        {
            return Err(format!("{} changed after review", file.name));
        }
    }
    Ok(())
}
fn external(command: Command) -> Result<(), String> {
    println!("$ {}", command.display());
    let status = process::std_command(&command).status().map_err(|e| e.to_string())?;
    if status.success() {
        Ok(())
    } else {
        Err(format!("{} failed ({status})", command.program))
    }
}
fn shell_quote(text: &str) -> String {
    format!("'{}'", text.replace('\'', "'\\''"))
}
/// Only this runner executes PKGBUILD. Its one-use approval must match the full
/// manifest, and every byte of every input is checked again immediately before use.
pub fn execute_manifest(path: &Path) -> Result<(), String> {
    let bytes = fs::read(path).map_err(|e| e.to_string())?;
    let plan: Plan = serde_json::from_slice(&bytes).map_err(|e| e.to_string())?;
    validate_root(&plan.root)?;
    if path != plan.root.join("transaction.json")
        || fs::read_to_string(plan.root.join("approved")).map_err(|_| "This transaction has no unused review approval")? != digest(&bytes)
    {
        return Err("Review approval does not match this transaction".into());
    }
    for unit in &plan.units {
        verify_files(&plan.root, unit)?;
    }
    fs::rename(plan.root.join("approved"), plan.root.join("running")).map_err(|e| e.to_string())?;
    for unit in &plan.units {
        verify_files(&plan.root, unit)?;
        let build = plan.root.join(&unit.base);
        let artifacts = plan.root.join(format!("{}-artifacts", unit.base));
        fs::create_dir(&artifacts).map_err(|e| e.to_string())?;
        let config = plan.root.join(format!("{}-makepkg.conf", unit.base));
        fs::write(
            &config,
            format!(
                "source /etc/makepkg.conf\nPACMAN_AUTH=(pkexec --disable-internal-agent)\nPKGDEST={}\n",
                shell_quote(&artifacts.to_string_lossy())
            ),
        )
        .map_err(|e| e.to_string())?;
        crate::polkit::ensure_running();
        external(
            Command::new(
                "env",
                [
                    format!("PKGDEST={}", artifacts.display()),
                    format!("BUILDDIR={}", build.display()),
                    format!("SRCDEST={}", build.display()),
                    "makepkg".into(),
                    "--config".into(),
                    config.to_string_lossy().into_owned(),
                    "--syncdeps".into(),
                    "--noconfirm".into(),
                    "--force".into(),
                    "--cleanbuild".into(),
                ],
            )
            .in_dir(&build),
        )?;
        let mut selected = BTreeMap::new();
        for entry in fs::read_dir(&artifacts).map_err(|e| e.to_string())?.flatten() {
            let path = entry.path();
            if path.to_string_lossy().contains(".pkg.tar") && !path.to_string_lossy().ends_with(".sig") {
                let output = process::run("pacman", &["-Qp", "--print-format", "%n", "--", &path.to_string_lossy()], process::SHORT);
                let name = output.stdout.trim();
                if output.success() && unit.packages.iter().any(|p| p == name) {
                    selected.insert(name.to_owned(), path);
                }
            }
        }
        if unit.packages.iter().any(|p| !selected.contains_key(p)) {
            return Err(format!("Build did not produce the reviewed packages: {}", unit.packages.join(", ")));
        }
        let mut args = vec!["pacman".to_owned(), "-U".into(), "--noconfirm".into()];
        if unit.as_dependencies {
            args.push("--asdeps".into());
        }
        args.push("--".into());
        args.extend(selected.values().map(|p| p.to_string_lossy().into_owned()));
        external(Command::privileged(args))?;
        if !unit.as_dependencies && !unit.dependency_packages.is_empty() {
            let mut args = vec!["pacman".to_owned(), "-D".into(), "--asdeps".into(), "--".into()];
            args.extend(unit.dependency_packages.clone());
            external(Command::privileged(args))?;
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn rpc_and_no_unreviewed_commands() {
        let p = parse_rpc(r#"{"type":"multiinfo","results":[{"Name":"example-bin","PackageBase":"example","Version":"1-1","Depends":["glibc"]}]}"#).unwrap();
        assert_eq!(p[0].base, "example");
        assert!(parse_rpc(r#"{"type":"error","error":"bad request"}"#).is_err());
        assert!(AurBackend.install_commands(&["example".into()]).is_empty());
        assert!(AurBackend.upgrade_commands(&[]).is_empty());
        assert!(!valid_name("--help"));
    }
    #[test]
    fn modified_files_fail_closed() {
        let root = std::env::temp_dir().join(format!("athena-review-test-{}", std::process::id()));
        let path = root.join("example");
        fs::create_dir_all(&path).unwrap();
        let text = "pkgname=example\n";
        fs::write(path.join("PKGBUILD"), text).unwrap();
        let unit = Unit {
            base: "example".into(),
            packages: vec!["example".into()],
            as_dependencies: false,
            dependency_packages: Vec::new(),
            files: vec![BuildFile {
                name: "PKGBUILD".into(),
                text: text.into(),
                previous: None,
                digest: digest(text.as_bytes()),
            }],
        };
        assert!(verify_files(&root, &unit).is_ok());
        fs::write(path.join("PKGBUILD"), "changed").unwrap();
        assert!(verify_files(&root, &unit).is_err());
        fs::write(path.join("PKGBUILD"), text).unwrap();
        fs::write(path.join("extra.install"), "bad").unwrap();
        assert!(verify_files(&root, &unit).is_err());
        fs::remove_dir_all(root).unwrap();
    }
    #[test]
    fn risk_markers_and_diffs() {
        assert!(!risky_lines("curl https://example.org | sh").is_empty());
        assert!(diff("old\n", "new\n").contains("-old\n+new"));
        assert!(!safe_file("../PKGBUILD"));
        assert!(!safe_file(".git/hooks/pre-commit"));
    }
    #[test]
    fn source_metadata_dependencies_are_architecture_specific() {
        let files = vec![BuildFile {
            name: ".SRCINFO".into(),
            text: "depends = common\ndepends_x86_64 = native64\ndepends_aarch64 = arm64\nmakedepends = compiler\n".into(),
            previous: None,
            digest: String::new(),
        }];
        assert_eq!(srcinfo_dependencies(&files, "x86_64"), ["common", "native64", "compiler"]);
    }
    #[test]
    fn unexpected_git_metadata_and_symlinks_stop_review_verification() {
        let root = std::env::temp_dir().join(format!("athena-review-extra-test-{}", std::process::id()));
        let directory = root.join("example");
        fs::create_dir_all(directory.join(".git")).unwrap();
        let text = "pkgname=example\n";
        fs::write(directory.join("PKGBUILD"), text).unwrap();
        let unit = Unit {
            base: "example".into(),
            packages: vec!["example".into()],
            as_dependencies: false,
            dependency_packages: Vec::new(),
            files: vec![BuildFile {
                name: "PKGBUILD".into(),
                text: text.into(),
                previous: None,
                digest: digest(text.as_bytes()),
            }],
        };
        assert!(verify_files(&root, &unit).is_err());
        fs::remove_dir_all(directory.join(".git")).unwrap();
        #[cfg(unix)]
        {
            std::os::unix::fs::symlink("PKGBUILD", directory.join("extra.install")).unwrap();
            assert!(verify_files(&root, &unit).is_err());
        }
        fs::remove_dir_all(root).unwrap();
    }
}

pub fn view_build_files(name: &str, is_aur: bool) -> Result<Vec<BuildFile>, String> {
    if !valid_name(name) {
        return Err("Invalid package name".into());
    }
    let (base, url) = if is_aur {
        let package = info(&[name.to_owned()])?.into_iter().next().ok_or("Package is not in the AUR")?;
        let url = format!("https://aur.archlinux.org/{}.git", package.base);
        (package.base, url)
    } else {
        let url = format!("https://archlinux.org/packages/search/json/?name={}", encode(name));
        let out = process::run(
            "curl",
            &[
                "--fail",
                "--silent",
                "--show-error",
                "--proto",
                "=https",
                "--max-time",
                "20",
                "--max-filesize",
                "1048576",
                &url,
            ],
            std::time::Duration::from_secs(25),
        );
        if !out.success() {
            return Err(out.combined());
        }
        let value: serde_json::Value = serde_json::from_str(&out.stdout).map_err(|e| e.to_string())?;
        let base = value["results"]
            .as_array()
            .and_then(|v| v.iter().find(|v| v["pkgname"].as_str() == Some(name)))
            .and_then(|v| v["pkgbase"].as_str())
            .filter(|v| valid_name(v))
            .ok_or("Repository package not found")?
            .to_owned();
        let url = format!("https://gitlab.archlinux.org/archlinux/packaging/packages/{base}.git");
        (base, url)
    };
    let root = cache_root()?.join("view").join(format!(
        "{}",
        SystemTime::now().duration_since(UNIX_EPOCH).map_err(|e| e.to_string())?.as_nanos()
    ));
    fs::create_dir_all(&root).map_err(|e| e.to_string())?;
    fetch_url(&base, &root, &url)
}
