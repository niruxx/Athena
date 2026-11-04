//! Athena's engine: the package sources it drives (pacman, dnf, apt,
//! Flatpak, Snap), how commands run, and the parsers for their output. No
//! GUI code lives here, so everything can be tested on its own.

pub mod backend;
pub mod backends;
pub mod distro;
pub mod package;
pub mod polkit;
pub mod process;
pub mod progress;

pub use backend::Backend;
pub use package::{OperationResult, PackageGroupInfo, PackageInfo, RepositoryAddField, RepositoryInfo, TransactionPreview};
pub use process::Command;

/// The version shown in About, from Cargo.toml.
pub const VERSION: &str = env!("CARGO_PKG_VERSION");

pub mod releases;
