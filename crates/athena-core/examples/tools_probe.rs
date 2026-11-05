//! Read-only smoke check: query explicit choices and round-trip their manifest.
//! No package, snapshot, cleanup or restore commands are executed.
fn main() -> Result<(), String> {
    let backends = athena_core::distro::all_backends();
    let manifest = athena_core::transfer::Manifest::collect(&backends)?;
    let json = manifest.json()?;
    let decoded = athena_core::transfer::Manifest::parse(&json)?;
    for backend in &backends {
        let count = decoded.packages.iter().filter(|p| p.source == backend.key()).count();
        println!("{}: {count} explicit choices", backend.name());
    }
    let missing = decoded.missing(&backends);
    println!("Round-trip: {} choices; {} missing", decoded.packages.len(), missing.len());
    if !missing.is_empty() {
        return Err(format!(
            "Unexpected missing package choices: {}",
            missing.iter().map(|(p, _)| format!("{}:{}", p.source, p.name)).collect::<Vec<_>>().join(", ")
        ));
    }
    Ok(())
}
