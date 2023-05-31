all:
	ls *.md | parallel 'make {/.}.html'
    

%.html: %.md
	cmark $< > $@
