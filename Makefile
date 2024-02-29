all:
	ls *.md | parallel 'make {/.}.html'
	ls *.dot | parallel 'make {/.}.svg'
	go run feed.go > feed.xml
    

%.svg: %.dot
	dot -T svg $< -o $@

%.html: %.md header.html footer.html
	cat header.html > $@
	pandoc --toc $< >> $@
	cat footer.html >> $@
