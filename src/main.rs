use notify::{RecursiveMode, Result};
use notify_debouncer_mini::new_debouncer;
use rouille::{Response, router, try_or_400, websocket};
use std::{ffi::OsString, path::Path, thread, time::Duration};

fn main() -> Result<()> {
    let (tx, rx) = crossbeam_channel::bounded::<String>(20);

    std::thread::spawn(move || {
        let md_ext: OsString = "md".into();

        let (etx, erx) = std::sync::mpsc::channel();

        let mut debouncer = new_debouncer(Duration::from_millis(20), etx).unwrap();

        debouncer
            .watcher()
            .watch(Path::new("."), RecursiveMode::Recursive)
            .unwrap();

        debouncer
            .watcher()
            .watch(Path::new("."), RecursiveMode::Recursive)
            .unwrap();
        // Block forever, printing out events as they come in
        for res in erx {
            match res {
                Ok(events) => {
                    for event in events {
                        if event.path.extension().unwrap_or_default() == md_ext {
                            println!("event: {:?}", event);
                            let file_path_str = event
                                .path
                                .file_stem()
                                .unwrap_or_default()
                                .to_string_lossy()
                                .to_string();
                            tx.send(file_path_str).unwrap();
                        }
                    }
                }
                Err(e) => println!("watch error: {:?}", e),
            }
        }
    });

    rouille::start_server("localhost:8001", move |request| {
        router!(request,
            (GET) (/) => {
                // The / route outputs an HTML client so that the user can try the websockets.
                // Note that in a real website you should probably use some templating system, or
                // at least load the HTML from a file.
                Response::html("<script type=\"text/javascript\">
                    var socket = new WebSocket(\"ws://localhost:8001/ws\", \"echo\");
                    socket.onmessage = function(event) {{
                        console.log(event.data);
                    }}
                    </script>
                    ")
            },

            (GET) (/ws) => {
                // This is the websockets route.

                // In order to start using websockets we call `websocket::start`.
                // The function returns an error if the client didn't request websockets, in which
                // case we return an error 400 to the client thanks to the `try_or_400!` macro.
                //
                // The function returns a response to send back as part of the `start_server`
                // function, and a `websocket` variable of type `Receiver<Websocket>`.
                // Once the response has been sent back to the client, the `Receiver` will be
                // filled by rouille with a `Websocket` object representing the websocket.

                let (response, websocket) = try_or_400!(websocket::start(request, Some("echo")));

                let rx = rx.clone();
                thread::spawn(move || {
                    // This line will block until the `response` above has been returned.
                    let ws = websocket.recv().unwrap();
                    websocket_handling_thread(ws, rx);
                });
                response
            },
            _ => rouille::Response::empty_404()
        )
    });
}

fn websocket_handling_thread(
    mut websocket: websocket::Websocket,
    rx: crossbeam_channel::Receiver<String>,
) {
    println!("new websocket");

    match rx.recv() {
        Ok(file_path) => {
            println!("path: {}", file_path);
            websocket.send_text(&file_path).unwrap();
        }
        Err(err) => {
            eprintln!("err recv: {}", err);
        }
    };
}
