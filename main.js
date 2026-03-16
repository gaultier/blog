import hljs from './highlight.min.js';
import * as cmake from './cmake.min.js';
import * as scheme from './scheme.min.js';
import * as x86asm from './x86asm.min.js';
import * as dockerfile from './dockerfile.min.js';

location.origin.includes("github") || (new EventSource("/blog/live-reload").onmessage = () => location.reload());

hljs.registerLanguage("cmake", cmake.default);
hljs.registerLanguage("scheme", scheme.default);
hljs.registerLanguage("x86asm", x86asm.default);
hljs.registerLanguage("dockerfile", dockerfile.default);
hljs.registerLanguage("odin", function(e) {
  return {
    aliases: ["odin", "odinlang", "odin-lang"],
    keywords: {
      keyword: "auto_cast bit_field bit_set break case cast context continue defer distinct do dynamic else enum fallthrough for foreign if import in map matrix not_in or_else or_return package proc return struct switch transmute type_of typeid union using when where",
      literal: "true false nil",
      built_in: "abs align_of cap clamp complex conj expand_to_tuple imag jmag kmag len max min offset_of quaternion real size_of soa_unzip soa_zip swizzle type_info_of type_of typeid_of"
    },
    illegal: "</",
    contains: [e.C_LINE_COMMENT_MODE, e.C_BLOCK_COMMENT_MODE, {
      className: "string",
      variants: [e.QUOTE_STRING_MODE, {
        begin: "'",
        end: "[^\\\\]'"
      }, {
        begin: "`",
        end: "`"
      }]
    }, {
      className: "number",
      variants: [{
        begin: e.C_NUMBER_RE + "[ijk]",
        relevance: 1
      }, e.C_NUMBER_MODE]
    }]
  }
});

hljs.registerLanguage("awk", function(e) {
  return {
    aliases: ["awk", "awklang", "awk-lang"],
    keywords: {
      keyword: "if else print break printf while for in return skip BEGIN END",
      literal: "",
      built_in: ""
    },
    illegal: "</",
    contains: [ hljs.COMMENT('#', '$'), ]
  }
});

hljs.registerLanguage("dtrace", function(e) {
  return {
    aliases: ["d", "dtracelang", "dtrace-lang"],
    keywords: {
      keyword: "if else self this BEGIN END typedef struct uintptr_t uint8_t uint16_t uint32_t uint64_t intptr_t int8_t int16_t int32_t int64_t size_t void",
      literal: "",
      built_in: "trace copyin copyinstr stringof uregs print printf exit rindex strlen quantize lquantize timestamp execname pid $target io start args"
    },
    illegal: "</",
    contains: [ hljs.COMMENT('//', '$'), ]
  }
});

document.querySelectorAll('.code-hl').forEach(el => {
  // then highlight each
  hljs.highlightElement(el);
});

document.body.style['color-scheme'] = 'light dark';
let colorScheme = localStorage.getItem('colorScheme');
if (colorScheme !== null) {
  document.body.style.colorScheme = colorScheme;
}

let dark_light_mode_button = document.querySelector('#dark-light-mode');
dark_light_mode_button.textContent = document.body.style.colorScheme === 'dark' ? '🔆' : '🌙' ;
dark_light_mode_button.addEventListener('click', function(e) {
  e.preventDefault();

  let cur = document.body.style.colorScheme || 'dark';
  if (cur === 'dark') {
    document.body.style.colorScheme = 'light';
    dark_light_mode_button.textContent = '🌙';
  } else {
    document.body.style.colorScheme = 'dark';
    dark_light_mode_button.textContent = '🔆';
  }

  localStorage.setItem('colorScheme', document.body.style.colorScheme);
});

document.querySelectorAll('.copy-code').forEach((el, _i) => {
  el.addEventListener('click', function(e){
    const pre = el.parentElement.parentElement;
    const codeBlock = pre.querySelector('code');
    navigator.clipboard.writeText(codeBlock.innerText);
  });
});
