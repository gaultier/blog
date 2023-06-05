all:
	ls *.md | parallel 'make {/.}.html'
	ls *.dot | parallel 'make {/.}.svg'
    

%.svg: %.dot
	dot -T svg $< -o $@

%.html: %.md
	cmark --unsafe $< > $@
