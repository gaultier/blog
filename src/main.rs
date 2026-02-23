use markdown::{
    ParseOptions,
    mdast::{Node, Text},
};
use notify::{RecursiveMode, Result};
use notify_debouncer_mini::new_debouncer;
use rouille::{router, try_or_400, websocket};
use std::{
    ffi::OsString,
    fs,
    path::{Path, PathBuf},
    thread,
    time::Duration,
};

fn md_collect_titles(node: &Node, titles: &mut Vec<(String, u8)>) {
    match node {
        Node::Root(root) => {
            for child in &root.children {
                md_collect_titles(child, titles);
            }
        }
        Node::Heading(heading) => {
            let level = heading.depth;
            // TODO: Handle markdown title!
            assert_eq!(1, heading.children.len());
            let child = heading.children.first().unwrap();
            let content = match child {
                Node::Text(Text { value, .. }) => value.clone(),
                other => panic!("unexpected value: {:#?}", other),
            };
            titles.push((content, level));
        }
        Node::Paragraph(paragraph) => {
            for child in &paragraph.children {
                md_collect_titles(child, titles);
            }
        }
        _ => {}
    }
}

fn main() -> Result<()> {
    let md_path = PathBuf::from("x11_x64.md");
    let md_content_bytes = fs::read(&md_path).unwrap();
    let md_content_utf8 = String::from_utf8(md_content_bytes).unwrap();
    let md_ast = markdown::to_mdast(&md_content_utf8, &ParseOptions::default()).unwrap();
    println!("{:#?}", md_ast);

    let mut titles = Vec::with_capacity(12);
    md_collect_titles(&md_ast, &mut titles);
    println!("titles: {:#?}", titles);

    rouille::start_server("localhost:8001", move |request| {
        {
            // The `match_assets` function tries to find a file whose name corresponds to the URL
            // of the request. The second parameter (`"."`) tells where the files to look for are
            // located.
            // In order to avoid potential security threats, `match_assets` will never return any
            // file outside of this directory even if the URL is for example `/../../foo.txt`.
            let response = rouille::match_assets(request, "..");

            // If a file is found, the `match_assets` function will return a response with a 200
            // status code and the content of the file. If no file is found, it will instead return
            // an empty 404 response.
            // Here we check whether if a file is found, and if so we return the response.
            if response.is_success() {
                return response;
            }
        }

        router!(request,



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

                let watching_path = request.url();
                thread::spawn(move || {
                    // This line will block until the `response` above has been returned.
                    let ws = websocket.recv().unwrap();
                    websocket_handling_thread(ws, watching_path);
                });
                response
            },
            _ => rouille::Response::empty_404()
        )
    });
}

fn websocket_handling_thread(mut websocket: websocket::Websocket, watching_path: String) {
    println!("new websocket");
    let (etx, erx) = std::sync::mpsc::channel();

    let mut debouncer = new_debouncer(Duration::from_millis(200), etx).unwrap();

    debouncer
        .watcher()
        .watch(Path::new("."), RecursiveMode::Recursive)
        .unwrap();

    let md_ext: OsString = "md".into();

    // Block forever, printing out events as they come in
    for res in erx {
        match res {
            Ok(events) => {
                for event in events {
                    let stem = event
                        .path
                        .file_stem()
                        .unwrap()
                        .to_string_lossy()
                        .to_string();
                    if event.path.extension().unwrap_or_default() == md_ext {
                        println!("event: {:?}", event);

                        let md_content_bytes = fs::read(&event.path).unwrap();
                        let md_content_utf8 = String::from_utf8(md_content_bytes).unwrap();
                        let md_ast = markdown::to_mdast(&md_content_utf8, &ParseOptions::default());
                        println!("{:#?}", md_ast);
                        let html_content = markdown::to_html(&md_content_utf8);

                        let html_path = event.path.clone().with_extension("html");
                        println!("write to: {:?}", &html_path);
                        fs::write(html_path, html_content).unwrap();

                        let file_path_str =
                            event.path.file_stem().unwrap_or_default().to_string_lossy();
                        websocket.send_text(&file_path_str).unwrap();
                        return;
                    }
                }
            }
            Err(e) => eprintln!("watch error: {:?}", e),
        }
    }
    println!("end of file watch & websocket handling");
}
