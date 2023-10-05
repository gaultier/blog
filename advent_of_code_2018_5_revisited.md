<link rel="stylesheet" type="text/css" href="main.css">
<a href="/blog">All articles</a>

# Optimizing a past solution for Advent of Code 2018 challenge  in assembly

A few days ago I was tweaking the appearance of this blog and I stumbled upon my [first article](/blog/advent_of_code_2018_5) which is about solving a simple problem from Advent of Code. I'll let you read the first paragraph to get to know the problem and come back here.

Immediately, I thought I could do better: 
- In the Lisp solution, there are lots of allocations and the code is not straightforward.
- In the C solution, there is no allocation apart from the input but we do a lot of unnecessary work.


This coincided with me listening to an interview from the VLC developers saying there wrote hundred of thousand of lines of (multi platform!) Assembly code by hand in their new AV1 decoder. I thought that was intriguing, who still writes assembly by hand in 2023? Well these guys are no idiots so I should try it as well.


I came up with a new algorithm, which on paper does less work. It's one linear pass on the input. We maintain two pointers, `current` and `next`, which we compare to decide whether we should merge the characters they point to. 'Merging'

`next` is always incremented by one in each loop iteration, that's the easy one.
`current` is always pointing to a character before `current`, but not always directly.

```


```
