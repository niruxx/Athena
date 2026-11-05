//! Normalize native history formats while retaining their original text.
use chrono::{DateTime, Local, NaiveDateTime, TimeZone};
use regex::Regex;
use std::sync::LazyLock;

#[derive(Debug, Clone)]
pub struct Entry {
    pub timestamp: Option<i64>,
    pub date: String,
    pub source: String,
    pub summary: String,
}

fn date(text: &str) -> Option<(i64, String)> {
    static DATE: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"\d{4}-\d\d-\d\d[T ]\s*\d\d:\d\d(?::\d\d)?(?:Z|[+-]\d\d:?\d\d)?").unwrap());
    let found = DATE.find(text)?.as_str();
    let normalized = found.split_whitespace().collect::<Vec<_>>().join(" ");
    if let Ok(value) = DateTime::parse_from_rfc3339(&normalized) {
        return Some((value.timestamp(), found.to_owned()));
    }
    if let Ok(value) = DateTime::parse_from_str(&normalized, "%Y-%m-%dT%H:%M:%S%z") {
        return Some((value.timestamp(), found.to_owned()));
    }
    for format in ["%Y-%m-%d %H:%M:%S", "%Y-%m-%d %H:%M", "%Y-%m-%dT%H:%M:%S", "%Y-%m-%dT%H:%M"] {
        if let Ok(value) = NaiveDateTime::parse_from_str(&normalized, format) {
            return Local.from_local_datetime(&value).earliest().map(|v| (v.timestamp(), found.to_owned()));
        }
    }
    None
}

pub fn parse(source: &str, text: &str) -> Vec<Entry> {
    let mut result = Vec::new();
    if source == "apt" {
        // The backend returns newest lines first, including inside a block.
        let mut lines: Vec<_> = text.lines().collect();
        if text.find("End-Date:").zip(text.find("Start-Date:")).is_some_and(|(end, start)| end < start) {
            lines.reverse();
        }
        let mut block = Vec::new();
        for line in lines {
            if line.starts_with("Start-Date:") {
                block.clear();
            }
            if !line.trim().is_empty() {
                block.push(line);
            }
            if line.starts_with("End-Date:") {
                let summary = block.join("\n");
                let parsed = date(&summary);
                result.push(Entry {
                    timestamp: parsed.as_ref().map(|d| d.0),
                    date: parsed.map(|d| d.1).unwrap_or_default(),
                    source: source.into(),
                    summary,
                });
                block.clear();
            }
        }
        return result;
    }
    for line in text.lines().filter(|line| !line.trim().is_empty()) {
        let parsed = date(line);
        let is_event = match source {
            "pacman" => {
                line.contains("[ALPM]")
                    && ["installed ", "upgraded ", "removed ", "downgraded ", "reinstalled "]
                        .iter()
                        .any(|verb| line.contains(verb))
            }
            "dnf" => line.trim_start().starts_with(|c: char| c.is_ascii_digit()) && (line.contains('|') || parsed.is_some()),
            "flatpak" => parsed.is_some(),
            "snap" => line.split_whitespace().next().is_some_and(|v| v.parse::<u64>().is_ok()),
            _ => false,
        };
        if is_event {
            result.push(Entry {
                timestamp: parsed.as_ref().map(|d| d.0),
                date: parsed.map(|d| d.1).unwrap_or_else(|| "Date as reported below".into()),
                source: source.into(),
                summary: line.trim().to_owned(),
            });
        }
    }
    result
}

pub fn sort(entries: &mut [Entry]) {
    entries.sort_by_key(|entry| std::cmp::Reverse(entry.timestamp));
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn native_formats() {
        let pacman = parse(
            "pacman",
            "[2026-02-03T11:30:00+0000] [ALPM] upgraded firefox (1 -> 2)\n[2026-02-03T11:30:00+0000] [PACMAN] Running pacman",
        );
        assert_eq!(pacman.len(), 1);
        assert!(pacman[0].timestamp.is_some());
        assert_eq!(
            parse("dnf", "ID | Command line | Date and time | Action\n2 | upgrade | 2026-02-03 11:30 | Upgrade").len(),
            1
        );
        assert_eq!(parse("flatpak", "2026-02-03 11:30:00\tinstall\torg.example.App").len(), 1);
        assert_eq!(
            parse(
                "snap",
                "ID Status Spawn Ready Summary\n12 Done today at 11:30 UTC today at 11:31 UTC Install example"
            )
            .len(),
            1
        );
        let apt = parse(
            "apt",
            "End-Date: 2026-02-03  11:31:00\nInstall: example:amd64 (1)\nStart-Date: 2026-02-03  11:30:00",
        );
        assert_eq!(apt.len(), 1);
        assert!(apt[0].timestamp.is_some());
    }
    #[test]
    fn merges_newest_first_and_keeps_undated_last() {
        let mut entries = parse("pacman", "[2026-02-03T11:30:00Z] [ALPM] installed example (1)");
        entries.extend(parse("flatpak", "2026-02-04T11:30:00Z\tinstall\torg.example.App"));
        entries.extend(parse("snap", "1 Done yesterday at 11:30 UTC - Install example"));
        sort(&mut entries);
        assert_eq!(entries[0].source, "flatpak");
        assert_eq!(entries[2].source, "snap");
    }
}
