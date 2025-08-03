Title: Flaky tests due to bad randomness?
Tags: JavaScript, Randomness, Gnuplot
---

Flaky tests are like a stone in your shoe, always reminding you they're here to annoy you. 

We have a big end-to-end test suite at work, and unfortunately some of these tests are flaky which means that there is small chance they will fail. Multiply this seemingly small probability by many developers, each one running the test suite many times a day, and this all means this flakiness will fail the test suite (and thus block the CI, and thus block a release) every single day.

There could be many reasons for this flakiness but I believe I found one reason with a simple fix: bad randomness.

You see, each test in our suite dutifully creates its own resources at the very start, so that it can be run in parallel with other tests, and adding a new test does not impact the existing ones. Resources have random names so that they are 'unique'. It looks something like that:

```js
const email = generateRandomEmail()
const accountName = generateRandomAccountName()
const account = createAccount(email, accountName)
```

But I noticed some tests were sometimes failing randomly: it seemed a test would accidentally act on resources belonging to another test. How could it be? Well, random names are only 'unique' (meaning, the chance of generate a name that already exists is very low, for some definition of low) if the random generator is 'good', for some definition of 'good'.

So let's see the implementation:

```js
function generateRandomEmail() {
  return Math.random().toString(36) + '@ory.sh'
}
```

Note: `.toString(36)` formats the number in base 36 (`a-zA-Z0-9`).

All the random functions were some variant of this. Hmm, how good is `Math.random`? It used to be pretty [bad](https://v8.dev/blog/math-random) but it then got better.

I thought, if this is the issue, then the fix should be quite simple and all tests would benefit from that fix.

One of the best advice I heard in the field of Software Engineering is:

> Find a way to visualize your problem. Humans are visual creatures.

So let's generate a lot of random values (say, 10 thousand) with our logic:


```js
const count = parseInt(process.argv[2])

for (let i=0; i < count; i++){
  const s = Math.random().toString(36);
  // `s` is of the form: `0.xxxx` so we skip the `0.` with `slice()`
  // since that does not add anything of value to our analysis.
  const n = parseInt(s.slice(2), 36)
  console.log(n);
}
```

Since `Math.random().toString(36)` generates a string, we parse it back to a number to be able to plot it.
 
And plot them with the venerable Gnuplot:

```
set terminal pngcairo size 800,600 enhanced font 'Arial,10' 
set output 'rand.png'
plot '~/scratch/rand.txt' with dots
```

We run our scripts:

```sh
$ node rand.js 10000 > ~/scratch/rand.txt
$ gnuplot rand.gp
$ open rand.png
```

And we get:

![10 thousand random values with Math.random().toString(36)](js_rand_bad.png)
