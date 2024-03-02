all:
	ls *.md | parallel 'make {/.}.html'
	ls *.dot | parallel 'make {/.}.svg'
	go run feed.go > feed.xml
    

%.svg: %.dot
	dot -T svg $< -o $@

%.html: %.md header.html footer.html
	printf '<!DOCTYPE html>\n<html>\n<head>\n<title>%s</title>\n' "$$(rg -m1 '^# (.+)$$' --only-matching --replace '$$1' --no-line-number --no-filename $<)" > $@
	cat header.html >> $@
	if [ "$<" != "index.md" ]; then printf '<p id="publication_date">Published on %s.</p>' $$(git log --format='%as' --reverse -- $< | head -n1)  >> $@; fi
	pandoc --toc $< >> $@
	cat footer.html >> $@
