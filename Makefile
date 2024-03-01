all:
	ls *.md | parallel 'make {/.}.html'
	ls *.dot | parallel 'make {/.}.svg'
	go run feed.go > feed.xml
    

%.svg: %.dot
	dot -T svg $< -o $@

%.html: %.md header.html footer.html
	cat header.html > $@
	printf '<p id="publication_date">Published on %s.</p>' $$(git log --format='%as' --reverse -- $< | head -n1)  >> $@
	pandoc --toc $< >> $@
	cat footer.html >> $@
