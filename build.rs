// Build libcmark-gfm from the `vendor/cmark-gfm` submodule into a static
// library, with the C compiler directly: no CMake, no crate.
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

// From `src/CMakeLists.txt` and `extensions/CMakeLists.txt` (`LIBRARY_SOURCES`).
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

// The headers CMake would generate from `config.h.in`,
// `cmark-gfm_version.h.in` and `generate_export_header` (for a static library,
// the export macro is empty).
const CONFIG_H: &str = "#ifndef CMARK_CONFIG_H
#define CMARK_CONFIG_H
#include <stdbool.h>
#define HAVE_STDBOOL_H
#define HAVE___BUILTIN_EXPECT
#define HAVE___ATTRIBUTE__
#define CMARK_ATTRIBUTE(list) __attribute__ (list)
#ifndef CMARK_INLINE
#define CMARK_INLINE inline
#endif
#endif
";
const EXPORT_H: &str = "#ifndef CMARK_GFM_EXPORT_H
#define CMARK_GFM_EXPORT_H
#define CMARK_GFM_EXPORT
#define CMARK_GFM_NO_EXPORT
#endif
";

fn cmake_version(cmake_lists: &str, name: &str) -> String {
    let prefix = format!("set({} ", name);
    let line = cmake_lists
        .lines()
        .find(|l| l.starts_with(&prefix))
        .unwrap_or_else(|| panic!("missing {} in CMakeLists.txt", name));
    line[prefix.len()..].trim_end_matches(')').trim().to_owned()
}

fn main() {
    let cmark_dir = Path::new(CMARK_DIR);
    let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());
    let include_dir = out_dir.join("include");
    let obj_dir = out_dir.join("obj");
    fs::create_dir_all(&include_dir).unwrap();
    fs::create_dir_all(&obj_dir).unwrap();

    println!("cargo:rerun-if-changed=build.rs");
    println!("cargo:rerun-if-changed={}/src", CMARK_DIR);
    println!("cargo:rerun-if-changed={}/extensions", CMARK_DIR);
    println!("cargo:rerun-if-changed={}/CMakeLists.txt", CMARK_DIR);
    println!("cargo:rerun-if-env-changed=CC");
    println!("cargo:rerun-if-env-changed=AR");

    let cmake_lists = fs::read_to_string(cmark_dir.join("CMakeLists.txt")).unwrap_or_else(|err| {
        panic!(
            "failed to read {}/CMakeLists.txt, is the submodule checked out (`git submodule update --init`)? err={}",
            CMARK_DIR, err
        )
    });
    let [major, minor, patch, gfm] = [
        "PROJECT_VERSION_MAJOR",
        "PROJECT_VERSION_MINOR",
        "PROJECT_VERSION_PATCH",
        "PROJECT_VERSION_GFM",
    ]
    .map(|name| cmake_version(&cmake_lists, name));
    let version_h = format!(
        "#ifndef CMARK_GFM_VERSION_H
#define CMARK_GFM_VERSION_H
#define CMARK_GFM_VERSION (({major} << 24) | ({minor} << 16) | ({patch} << 8) | {gfm})
#define CMARK_GFM_VERSION_STRING \"{major}.{minor}.{patch}.gfm.{gfm}\"
#endif
"
    );
    fs::write(include_dir.join("config.h"), CONFIG_H).unwrap();
    fs::write(include_dir.join("cmark-gfm_export.h"), EXPORT_H).unwrap();
    fs::write(include_dir.join("cmark-gfm_version.h"), version_h).unwrap();

    let cc = env::var("CC").unwrap_or_else(|_| "cc".to_owned());
    let ar = env::var("AR").unwrap_or_else(|_| "ar".to_owned());
    let opt_level = env::var("OPT_LEVEL").unwrap_or_else(|_| "0".to_owned());
    let debug = env::var("DEBUG").is_ok_and(|d| d != "false" && d != "0");

    // Compile every file in parallel: they are independent.
    let mut objects = Vec::new();
    let mut children: Vec<(Child, &str)> = Vec::new();
    for file in SRC_FILES.iter().chain(EXTENSION_FILES.iter()) {
        let obj = obj_dir.join(file.replace(['/', '.'], "_") + ".o");
        let mut cmd = Command::new(&cc);
        cmd.arg("-std=c11")
            .arg(format!("-O{}", opt_level))
            .arg("-fPIC")
            .arg("-DNDEBUG")
            .arg("-w")
            .arg("-I")
            .arg(&include_dir)
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
        children.push((
            cmd.spawn()
                .unwrap_or_else(|err| panic!("failed to run {}: {}", cc, err)),
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
    let status = Command::new(&ar)
        .arg("crs")
        .arg(&lib)
        .args(&objects)
        .status()
        .unwrap_or_else(|err| panic!("failed to run {}: {}", ar, err));
    assert!(
        status.success(),
        "failed to archive libcmark-gfm: {}",
        status
    );

    println!("cargo:rustc-link-search=native={}", out_dir.display());
    println!("cargo:rustc-link-lib=static=cmark-gfm");
}
