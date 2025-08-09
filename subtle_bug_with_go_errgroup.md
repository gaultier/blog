Title: Subtle bug with Go's errgroup
Tags: Go
---

Yesterday I got bitten by an insidious bug at work. Fortunately a test caught it before it got merged. The more I work on big, complex software, the more I deeply appreciate tests, even though I do not necessarily enjoy writing them. Anyways, I lost a few hours investigating this issue, and this could happen to anyone, I think. 

Anyways let's go into it. I minimized the issue to a stand-alone program. 

## The program

Today, we are writing a program validating passwords. Well, the most minimal version thereof. It contains the old password, takes the new password on the command line, and runs a few checks to see if this password is fine:

- Checks if the new password is different from the old password. This can catch the case where the old password has leaked, we want to change it, and inadvertently use the same value as before. Which would leave us exposed.
- Check the Have I Been Pawned API, which stores millions of leaked passwords. This serves to avoid commonly used and leaked passwords. The real production program has a in-memory cache in front of the API for performance, but we still have to do an API call at start-up and from time to time.
- Check that the password is long enough

For simplicity, the Have I Been Pawned API in our reproducer is just a text file with passwords in clear (do not do that in production!).

One last thing: passwords are (obviously, I hope) never stored in clear, and we instead store a hash using a [password hashing function](https://en.wikipedia.org/wiki/Bcrypt) specially designed to take up a lot of computational power to hinder brute-force attacks. Typically, that can take hundreds of milliseconds or even seconds (depending on the cost factor) for one hash.

For performance, if we have to compute this hash, we try to do other things in parallel. To achieve this, we use an [errgroup](https://pkg.go.dev/golang.org/x/sync/errgroup), which has become pretty common place now.

Here goes:

```go
package main

import (
	"context"
	"fmt"
	"io"
	"net/http"
	"os"
	"slices"
	"strings"

	"golang.org/x/crypto/bcrypt"
	"golang.org/x/sync/errgroup"
)

// Best effort: if the external API is down, swallow the error: the user should be able to change their password nonetheless.
func checkHaveIBeenPawned(ctx context.Context, pw string) error {
	req, err := http.NewRequestWithContext(ctx, "GET", "http://localhost:8000/haveibeenpawned.txt", strings.NewReader(pw))
	if err != nil {
		return nil
	}

	resp, err := http.DefaultClient.Do(req)

	if err != nil {
		return nil
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return nil
	}

	respBody, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil
	}
	lines := strings.Split(string(respBody), "\n")

	if slices.Contains(lines, pw) {
		return fmt.Errorf("the password appears in a leak")
	}

	return nil
}

func changePassword(ctx context.Context, oldHash []byte, newPassword string) ([]byte, error) {
	var newHash []byte

	g, ctx := errgroup.WithContext(ctx)
	// Do in parallel:
	// - Compute the hash of the new password
	// - Check that the new password is not the same as the old password
	g.Go(func() error {
		var err error
		newHash, err = bcrypt.GenerateFromPassword([]byte(newPassword), 10)
		return err
	})
	g.Go(func() error {
		if err := bcrypt.CompareHashAndPassword(oldHash, []byte(newPassword)); err == nil {
			return fmt.Errorf("old and new password are the same")
		}
		return nil
	})

	if err := g.Wait(); err != nil {
		return nil, err
	}

	if err := checkHaveIBeenPawned(ctx, newPassword); err != nil {
		return nil, err
	}

	// Reject passwords that are too short.
	// In real programs, this value should be much higher!
	if len(newPassword) < 4 {
		return nil, fmt.Errorf("password is too short")
	}

	return newHash, nil
}

func main() {
	ctx := context.Background()

	oldPassword := "hello, world"
	oldHash, err := bcrypt.GenerateFromPassword([]byte(oldPassword), 8)
	if err != nil {
		panic(err)
	}

	newPassword := os.Args[1]
	newHash, err := changePassword(ctx, oldHash, newPassword)
	if err != nil {
		fmt.Printf("failed to change password: %v", err)
	} else {
		fmt.Printf("new password set: %s", string(newHash))
	}
}

```

I think it is pretty straightforward. The only 'clever' thing is using the errgroup, which cancels all tasks as soon as one fails. This is handy to avoid doing unecessary expensive computations.

 We serve the static text file `haveibeenpawned.txt` using a HTTP server to act as the Have I Been Pawned API, it just contains one password per line e.g.:

```txt
hello
abc123
123456
```
Let's try it then:

```sh
# Serve the leaked password file
$ python3 -m http.server -d . &

# Fine new password, succeeds
$ go run main.go 'correct horse battery staple'
new password set: $2a$10$6tIRwMIKaOZuzsT1dT3EVu5boCtautewpJYT6r5Fc6PV13i9ezcNS

# Password too short
$ go run main.go 'hi'
failed to change password: password is too short

# Leaked password
$ go run main.go 'hello'
new password set: $2a$10$pmOOg5VOFEIjFk47hstoQeFbgHFTzxChpsp77SuuE4yvlSK9Ds4SG⏎                         
```


Wait, the last one should have been rejected, what's going on? 

## Bug investigation


We can use strace/dtrace, add logs, or simply look at the python static HTTP server, the verdict is the same: no request is made. How is this possible? I can see the Go function call!

At first I thought a data race was happening between goroutines, having been recently [burnt by that](/blog/a_subtle_data_race_in_go.html). But it turned out it was something different.

Let's log the errors inside `checkHaveIBeenPawned` that we so conveniently swallowed:

```diff
diff --git a/main.go b/main.go
index ae602df..17d0228 100644
--- a/main.go
+++ b/main.go
@@ -23,6 +23,7 @@ func checkHaveIBeenPawned(ctx context.Context, pw string) error {
        resp, err := http.DefaultClient.Do(req)
 
        if err != nil {
+               fmt.Fprintf(os.Stderr, "http request error: %v\n", err)
                return nil
        }
        defer resp.Body.Close()
```

And we see:

```text
http request error: Get "http://localhost:8000/haveibeenpawned.txt": context canceled
```

Uh...what? We do not even have a timeout set! How can the context be canceled?

At that point, a great collegue of mine helped me debug and found the issue. He sent me this one line from the `errgroup` [documentation](https://pkg.go.dev/golang.org/x/sync/errgroup#Group.WithContext):

> The derived Context is canceled the first time a function passed to Go returns a non-nil error or the first time Wait returns, whichever occurs first. 

Ah... that's why. I mean, it makes sense that the context is canceled on the first error occurring, that's how operations in other goroutines can notice and also stop early. It's just surprising to me that this happens also when `Wait` returns and no error happened.

Ok, how do we fix it then? 

This was my fix in the real program: since the HTTP call is the one that does not get to run, and this could take up some time, why not also run it in a goroutine in the `errgroup`? This task is completely independent from the others.

Here it is, quite a short fix:

```diff
diff --git a/main.go b/main.go
index ae602df..74371a5 100644
--- a/main.go
+++ b/main.go
@@ -62,15 +62,14 @@ func changePassword(ctx context.Context, oldHash []byte, newPassword string) ([]
                }
                return nil
        })
+       g.Go(func() error {
+               return checkHaveIBeenPawned(ctx, newPassword)
+       })
 
        if err := g.Wait(); err != nil {
                return nil, err
        }
 
-       if err := checkHaveIBeenPawned(ctx, newPassword); err != nil {
-               return nil, err
-       }
-
        // Reject passwords that are too short.
        // In real programs, this value should be much higher!
        if len(newPassword) < 4 {
```


Let's test it then:

```sh
 $ go run main.go 'hi'
failed to change password: password is too short

$ go run main.go 'hello'
failed to change password: the password appears in a leak

$ go run main.go 'correct horse battery staple'
new password set: $2a$10$.oyEO/cSmTWugfwdpoADYOB/AM.uHjz1HodOysS3ksIS.FS4RvTx.⏎                   
```

It works correctly now!


## Alternative fix

What if I told you there is a one character change that fixes the issue?

```diff
diff --git a/main.go b/main.go
index ae602df..6bcbfa3 100644
--- a/main.go
+++ b/main.go
@@ -47,7 +47,7 @@ func checkHaveIBeenPawned(ctx context.Context, pw string) error {
 func changePassword(ctx context.Context, oldHash []byte, newPassword string) ([]byte, error) {
        var newHash []byte
 
-       g, ctx := errgroup.WithContext(ctx)
+       g, _ := errgroup.WithContext(ctx)
        // Do in parallel:
        // - Compute the hash of the new password
        // - Check that the new password is not the same as the old password
```

Why does it work? Well, the `ctx` we get from `errgroup.WithContext(ctx)` is a child of the `ctx` passed to our function, and also shadows it in the current scope. Then, when we call `checkHaveIBeenPawned(ctx, newPassword)`, we use this child context that just got canceled by `g.Wait()` the line before. By not shadowing the parent context, the same call `checkHaveIBeenPawned(ctx, newPassword)` now uses this parent context, which has *not* been canceled in any way. So it works.

## Conclusion

The `errgroup` concept is pretty great. It greatly simplifies equivalent code written using Go channels which get real hairy real soon. But, as it is often the case in Go, you do need to read the fine print, because the type system is not expressive enough to reflect the pre- and post-conditions of the API.

Shadowing is another concept that made this issue less visible. I have had quite a few bugs due to shadowing in Go and Rust, both languages that idiomatically use it a lot. Some newer programming languages have outright banned variable shadowing, like Zig.

If you've ever heard of linear types, and never saw their utility, that's actually exactly what they are good for: a variable gets 'consumed' by a function, and the type system prevents us from using it after that point. Conceptually, `g.Wait(ctx)` consumes `ctx` and there is no point using this `ctx` afterwards. But the current Go type system does not prevent us from doing this, at all. 

Things get even muddier when we notice that after `g.Wait(ctx)`, we do the password length check, and that gets to run, contrary to the `checkHaveIBeenPawned` call. Since the length check does not care about a context, it runs just fine. This might be a case of Go actually having function coloring! The API code and the length check code get treated differently. 

So my take-away: any time a Go function takes a context as a parameter, you must assume that this function might not run fully. Some parts of this function may run (the parts not depending on the context), and some may not, because they will immediately result in an error and Go encourages returning early on error. 
