//! Prints what Athena's backends see on this machine, without changing
//! anything: `cargo run -p athena-core --example probe [search term]`.

use athena_core::distro;

fn main() {
    let query = std::env::args().nth(1).unwrap_or_else(|| "firefox".into());
    println!("Distribution family: {:?}", distro::detect_family());
    for backend in distro::all_backends() {
        let installed = backend.list_installed();
        let updates = backend.list_updates();
        let results = backend.search(&query);
        let repos = backend.list_repositories();
        println!(
            "{:<22} {:>5} installed, {:>3} updates, {:>4} results for {query:?}, {:>2} repositories, {} groups",
            backend.name(),
            installed.len(),
            updates.len(),
            results.len(),
            repos.len(),
            backend.list_groups().len()
        );
        if let Some(first) = results.first() {
            println!("    first result: {} {} ({}) — {}", first.name, first.available_version, first.repository, first.description);
        }
    }
}
