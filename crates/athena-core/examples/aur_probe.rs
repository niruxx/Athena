//! Read-only AUR planning probe. Never approves or executes build files.
//! Use XDG_CACHE_HOME/XDG_STATE_HOME pointing to temporary directories for CI.
fn main() {
    let names = std::env::args().skip(1).collect::<Vec<_>>();
    match athena_core::aur::prepare(&names) {
        Ok(plan) => {
            println!("Prepared {} build units in {}", plan.units.len(), plan.root.display());
            for unit in plan.units {
                println!(
                    "{}: {} files; packages {}; dependency={}",
                    unit.base,
                    unit.files.len(),
                    unit.packages.join(", "),
                    unit.as_dependencies
                );
            }
        }
        Err(error) => {
            eprintln!("Cannot prepare AUR review: {error}");
            std::process::exit(1);
        }
    }
}
