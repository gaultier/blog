use notify::{RecursiveMode, Result};
use notify_debouncer_mini::new_debouncer;
use std::{ffi::OsString, path::Path, time::Duration};

fn main() -> Result<()> {
    let md_ext: OsString = "md".into();

    let (tx, rx) = std::sync::mpsc::channel();

    let mut debouncer = new_debouncer(Duration::from_millis(20), tx).unwrap();

    debouncer
        .watcher()
        .watch(Path::new("."), RecursiveMode::Recursive)
        .unwrap();

    debouncer
        .watcher()
        .watch(Path::new("."), RecursiveMode::Recursive)?;
    // Block forever, printing out events as they come in
    for res in rx {
        match res {
            Ok(events) => {
                for event in events {
                    if event.path.extension().unwrap_or_default() == md_ext {
                        println!("event: {:?}", event)
                    }
                }
            }
            Err(e) => println!("watch error: {:?}", e),
        }
    }

    Ok(())
}
