Title: An amusing blind spot in Go's static analysis
Tags: Go
---

*Discussions: [/r/golang](https://old.reddit.com/r/golang/comments/1mx1sep/an_amusing_blind_spot_in_gos_static_analysis/?)*.

A short one today. I stumbled upon this Go test at work:

```go
func TestCacheHandling(t *testing.T) {
	router := NewRouterPublic()
	ts := httptest.NewServer(router)
	t.Cleanup(ts.Close)

	router.GET("/foo", func(w http.ResponseWriter, r *http.Request, _ httprouter.Params) {
		w.WriteHeader(http.StatusNoContent)
	})
	router.DELETE("/foo", func(w http.ResponseWriter, r *http.Request, _ httprouter.Params) {
		w.WriteHeader(http.StatusNoContent)
	})
	router.POST("/foo", func(w http.ResponseWriter, r *http.Request, _ httprouter.Params) {
		w.WriteHeader(http.StatusNoContent)
	})
	router.PUT("/foo", func(w http.ResponseWriter, r *http.Request, _ httprouter.Params) {
		w.WriteHeader(http.StatusNoContent)
	})
	router.PATCH("/foo", func(w http.ResponseWriter, r *http.Request, _ httprouter.Params) {
		w.WriteHeader(http.StatusNoContent)
	})

	for _, method := range []string{} {
		req, _ := http.NewRequest(method, ts.URL+"/foo", nil)
		res, err := ts.Client().Do(req)
		require.NoError(t, err)
		assert.EqualValues(t, "0", res.Header.Get("Cache-Control"))
	}
}
```

This test checks that a middleware adds the HTTP header `Cache-Control` for all routes. This should happen when registering a HTTP handler with `router.GET`, `router.DELETE`, etc. Why and how, that does not matter.

So, did you notice the issue with this test? I initially did not. I tweaked the testing assert near the end to a different value but this test still passed. Uh, what? 

What if I told you this test does nothing? 

Because the `for` loop iterates on an empty array, so there are zero loop iterations. 

What the developer intended was:

```diff
diff --git a/x/httprouterx/router_test.go b/x/httprouterx/router_test.go
index 0f9dba5515..48d2873157 100644
--- a/x/httprouterx/router_test.go
+++ b/x/httprouterx/router_test.go
@@ -72,7 +72,7 @@ func TestAdminPrefix(t *testing.T) {
 		w.WriteHeader(http.StatusNoContent)
 	})
 
-	for _, method := range []string{} {
+	for _, method := range []string{"GET", "DELETE", "POST", "PUT", "PATCH"} {
 		req, _ := http.NewRequest(method, ts.URL+"/admin/foo", nil)
 		res, err := ts.Client().Do(req)
 		require.NoError(t, err)

```

It's a trivial [fix](https://github.com/ory/kratos/commit/48f5adb9ce720f6906283372515b85f365a7f0b5#diff-083558e2f6efe440baaea9a5f2a1344a88f165216c261057e37249c89c276902L71).

I think that is visually easy to miss because in Go, an array literal is defined with curly braces, often on multiple lines, so the beginning of the `for` loop body looks very similar.


Interestingly no linter catches this issue. In the meantime you can use this regexp to catch instances of this issue, which is what I did and I discovered a few more cases in the codebase:

```sh
$ rg -t go 'for .* range \[\].+\{\} \{'
```


It's vexing because the Go compiler detects that this `for` loop is a no-op and optimizes it away, if you look at the generated assembly. But it generates no warning.

Does your programming language prevent this issue?

