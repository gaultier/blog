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

hljs.highlightAll();

document.querySelectorAll('code').forEach((el, _i) => {
  if (el.parentElement.tagName != "PRE"){ 
    return; 
  }

  var copy_btn = document.createElement('button');
  copy_btn.innerText = 'Copy';
  copy_btn.type = 'button';
  copy_btn.style.margin = '0.1rem';
  copy_btn.style['align-self'] = 'flex-end';

  // Copy original content before adding line numbers.
  var content = el.innerText.slice();
  copy_btn.addEventListener('click', function(e){
    navigator.clipboard.writeText(content);
  });
  el.parentElement.prepend(copy_btn);

  var lines = el.innerHTML.trimEnd().split('\n');
  var out = [];
  lines.forEach(function(l, i){
    out.push('<span class="line-number">' + (i+1).toString() + '</span> ' + l);
  });
  el.innerHTML = out.join('\n');
});
