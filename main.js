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

hljs.highlightAll();

document.body.style['color-scheme'] = 'light dark';
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
});

document.querySelectorAll('code').forEach((el, _i) => {
  if (el.parentElement.tagName == "PRE"){
    var header = document.createElement('div');
    header.style['display'] = 'inline-flex';
    header.style['align-items'] = 'center';
    header.style['justify-content'] = 'space-between';
    header.style['background-color'] = '#BDBDBD';
    header.style['align-self'] = 'stretch';

    var header_text = document.createElement('span');
    var lang = 'text';
    var prefix = 'language-';
    for (c of el.classList) {
      if (c.startsWith(prefix)) {
        lang = c.slice(prefix.length);
        break;
      }
    }
    header_text.innerText = lang[0].toUpperCase() + lang.slice(1);
    header_text.style['margin-left'] = '.2rem';
    header_text.style['color'] = 'white';
    header.appendChild(header_text);


    var copy_btn = document.createElement('button');
    copy_btn.innerHTML = `<svg aria-hidden="true" focusable="false" class="octicon octicon-copy" viewBox="0 0 16 16" width="16" height="16" fill="currentColor" display="inline-block" overflow="visible" style="vertical-align: text-bottom;"><path d="M0 6.75C0 5.784.784 5 1.75 5h1.5a.75.75 0 0 1 0 1.5h-1.5a.25.25 0 0 0-.25.25v7.5c0 .138.112.25.25.25h7.5a.25.25 0 0 0 .25-.25v-1.5a.75.75 0 0 1 1.5 0v1.5A1.75 1.75 0 0 1 9.25 16h-7.5A1.75 1.75 0 0 1 0 14.25Z"></path><path d="M5 1.75C5 .784 5.784 0 6.75 0h7.5C15.216 0 16 .784 16 1.75v7.5A1.75 1.75 0 0 1 14.25 11h-7.5A1.75 1.75 0 0 1 5 9.25Zm1.75-.25a.25.25 0 0 0-.25.25v7.5c0 .138.112.25.25.25h7.5a.25.25 0 0 0 .25-.25v-7.5a.25.25 0 0 0-.25-.25Z"></path></svg>`;
    copy_btn.type = 'button';
    copy_btn.style.margin = '0.1rem';
    copy_btn.style['align-self'] = 'flex-end';

    // Copy original content before adding line numbers.
    var content = el.innerText.slice();
    copy_btn.addEventListener('click', function(e){
      navigator.clipboard.writeText(content);
    });
    header.appendChild(copy_btn);
    el.parentElement.prepend(header);
  }


  if (0 == el.classList.length || el.classList.contains('language-sh') || el.classList.contains('language-shell') || el.classList.contains('language-bash') || el.classList.contains('language-ini') || el.classList.contains('language-diff')){ 
    return; 
  }


  var lines = el.innerHTML.trimEnd().split('\n');
  if (lines.length <= 1) {
    return;
  }
  var out = [];
  lines.forEach(function(l, i){
    out.push('<span class="line-number">' + (i+1).toString() + '</span> ' + l);
  });
  el.innerHTML = out.join('\n');
});
