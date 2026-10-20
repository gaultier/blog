// Build libcmark-gfm from `vendor/cmark-gfm` into a static library, with the C
// compiler directly: no CMake, no crate.
//
// Only the parser and the GFM extensions are compiled; the renderers are kept
// because `cmark.c` and the extensions refer to them, and the linker drops what
// `main.rs` does not use.

use std::{
    env, fs,
    path::{Path, PathBuf},
    process::{Child, Command},
};

const CMARK_DIR: &str = "vendor/cmark-gfm";

// Every source of the library; `src/main.c`, the command line tool, is not
// vendored.
const SRC_FILES: [&str; 27] = [
    "src/cmark.c",
    "src/node.c",
    "src/iterator.c",
    "src/blocks.c",
    "src/inlines.c",
    "src/scanners.c",
    "src/utf8.c",
    "src/buffer.c",
    "src/references.c",
    "src/footnotes.c",
    "src/map.c",
    "src/render.c",
    "src/man.c",
    "src/xml.c",
    "src/html.c",
    "src/commonmark.c",
    "src/plaintext.c",
    "src/latex.c",
    "src/houdini_href_e.c",
    "src/houdini_html_e.c",
    "src/houdini_html_u.c",
    "src/cmark_ctype.c",
    "src/arena.c",
    "src/linked_list.c",
    "src/syntax_extension.c",
    "src/registry.c",
    "src/plugin.c",
];
const EXTENSION_FILES: [&str; 7] = [
    "extensions/core-extensions.c",
    "extensions/table.c",
    "extensions/strikethrough.c",
    "extensions/autolink.c",
    "extensions/tagfilter.c",
    "extensions/ext_scanners.c",
    "extensions/tasklist.c",
];

fn main() {
    let cmark_dir = Path::new(CMARK_DIR);
    let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());
    let obj_dir = out_dir.join("obj");
    fs::create_dir_all(&obj_dir).unwrap();

    println!("cargo:rerun-if-changed=build.rs");
    println!("cargo:rerun-if-changed={}", CMARK_DIR);
    let target = env::var("TARGET").unwrap();
    let host = env::var("HOST").unwrap();
    // A command from the environment, the target-specific variable first
    // (`CC_aarch64_apple_darwin`), as the `cc` crate reads them. It may carry
    // arguments of its own (`CC="ccache clang"`).
    let tool = |name: &str, default: &str| -> Vec<String> {
        let specific = format!("{}_{}", name, target.replace('-', "_"));
        println!("cargo:rerun-if-env-changed={}", specific);
        println!("cargo:rerun-if-env-changed={}", name);
        let value = env::var(&specific).ok().or_else(|| {
            // The plain variable, or the default, is for the host: using it to
            // cross-compile would build objects that cannot be linked.
            assert!(
                target == host,
                "cross-compiling to {} needs {} to be set",
                target,
                specific
            );
            env::var(name).ok()
        });
        let value = value.unwrap_or_else(|| default.to_owned());
        let res: Vec<String> = value.split_whitespace().map(|s| s.to_owned()).collect();
        assert!(!res.is_empty(), "{} is empty", name);
        res
    };

    let cc = tool("CC", "cc");
    let ar = tool("AR", "ar");
    let opt_level = env::var("OPT_LEVEL").unwrap_or_else(|_| "0".to_owned());
    let debug = env::var("DEBUG").is_ok_and(|d| d != "false" && d != "0");
    // Extra flags, e.g. `-fsanitize=address` to match `-Zsanitizer=address`.
    let cflags_specific = format!("CFLAGS_{}", target.replace('-', "_"));
    println!("cargo:rerun-if-env-changed={}", cflags_specific);
    println!("cargo:rerun-if-env-changed=CFLAGS");
    let cflags = env::var(&cflags_specific)
        .or_else(|_| env::var("CFLAGS"))
        .unwrap_or_default();

    // Compile every file in parallel: they are independent.
    let mut objects = Vec::new();
    let mut children: Vec<(Child, &str)> = Vec::new();
    for file in SRC_FILES.iter().chain(EXTENSION_FILES.iter()) {
        let obj = obj_dir.join(file.replace(['/', '.'], "_") + ".o");
        let mut cmd = Command::new(&cc[0]);
        cmd.args(&cc[1..]);
        cmd.arg("-std=c11")
            .arg(format!("-O{}", opt_level))
            .arg("-fPIC")
            .arg("-DNDEBUG")
            .arg("-w")
            .arg("-I")
            .arg(cmark_dir.join("src"))
            .arg("-I")
            .arg(cmark_dir.join("extensions"))
            .arg("-c")
            .arg(cmark_dir.join(file))
            .arg("-o")
            .arg(&obj);
        if debug {
            cmd.arg("-g");
        }
        cmd.args(cflags.split_whitespace());
        children.push((
            cmd.spawn()
                .unwrap_or_else(|err| panic!("failed to run {:?}: {}", cc, err)),
            file,
        ));
        objects.push(obj);
    }
    for (mut child, file) in children {
        let status = child.wait().unwrap();
        assert!(status.success(), "failed to compile {}: {}", file, status);
    }

    let lib = out_dir.join("libcmark-gfm.a");
    let _ = fs::remove_file(&lib);
    let status = Command::new(&ar[0])
        .args(&ar[1..])
        .arg("crs")
        .arg(&lib)
        .args(&objects)
        .status()
        .unwrap_or_else(|err| panic!("failed to run {:?}: {}", ar, err));
    assert!(
        status.success(),
        "failed to archive libcmark-gfm: {}",
        status
    );

    println!("cargo:rustc-link-search=native={}", out_dir.display());
    println!("cargo:rustc-link-lib=static=cmark-gfm");
}
