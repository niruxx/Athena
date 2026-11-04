//! Compiles the GSettings schema into OUT_DIR so debug builds (`cargo run`)
//! find it without installing it. Packages install the XML under
//! /usr/share/glib-2.0/schemas instead, and never use this copy.

use std::path::Path;
use std::process::Command;

fn main() {
    let data = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../data");
    let schema = data.join("io.github.niruxx.Athena.gschema.xml");
    println!("cargo:rerun-if-changed={}", schema.display());
    let out = std::env::var("OUT_DIR").unwrap();
    let dir = Path::new(&out).join("schemas");
    std::fs::create_dir_all(&dir).unwrap();
    std::fs::copy(&schema, dir.join("io.github.niruxx.Athena.gschema.xml")).unwrap();
    match Command::new("glib-compile-schemas").arg("--strict").arg(&dir).status() {
        Ok(status) if status.success() => {}
        _ => println!("cargo:warning=glib-compile-schemas failed; debug builds will run without settings unless the schema is installed"),
    }
    println!("cargo:rustc-env=ATHENA_SCHEMA_DIR={}", dir.display());
}
