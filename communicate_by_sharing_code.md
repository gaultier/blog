# Communicate by sharing code

This is a grandiloquent title for a small trick that I've been using daily for years now, in every place I've worked at. 

Whenever there is a technical discussion, I think it really helps to look at existing code to anchor the debate in reality and make it concrete.

Screen sharing may work at times but I have found a low-tech solution: Share a link to a region of code in the codebase. It's easy and can be used in documentation and PRs as well. 

Every Version Control System (VCS) web UI worth its salt has that feature, let's take Github for example: [https://github.com/gaultier/micro-kotlin/blob/master/class_file.h#L773-L775](https://github.com/gaultier/micro-kotlin/blob/master/class_file.h#L773-L775)

The hurdle is that every hosting provider has its own URL shape to do so and that's not always documented, so there is a tiny bit of reverse-engineering involved. Compare the previous URL with this one: [https://gitlab.com/philigaultier/jvm-bytecode/-/blob/master/class_file.h?ref_type=heads#L125-127](https://gitlab.com/philigaultier/jvm-bytecode/-/blob/master/class_file.h?ref_type=heads#L125-127). It's slightly different.

So to make it easy to share a link to some code with coworkers, I've written a tiny script to craft the URL for me, inside my editor. I select a few lines, hit a keystroke, and the URL is now in the clipboard for me to paste it anywhere.

Since I use Neovim and Lua, this is what I'll cover, but I'm sure any editor can do that. Now that I think of it, there should be an existing extension for this? Back when I started using this trick I remember searching for one and finding nothing.

This article could also serve as a gentle introduction to using Lua in Neovim. The code is also directly mappable to Vimscript, Vim9 script or anything really.

So first thing first we need to create a user command to invoke this functionality and later map it to a keystroke:

```lua
vim.api.nvim_create_user_command('GitWebUiUrlCopy', function(arg)
end,
{force=true, range=true, nargs=0, bang=true, desc='Copy to clipboard a URL to a git webui for the current line'})
```

- `force=true` overrides any previous definition which is handy when iterating over the implementation
- `range=true` allows for selecting multiple lines and calling this command on the line range, but it also works when not selecting anything (in normal mode)
- `nargs=0` means that no argument is passed to the command

We pass a callback to `nvim_create_user_command` which will be called when we invoke the command. For now it does nothing but we are going to implement it in a second.

`arg` is an object containing for our purposes the line start and line end numbers:

```lua
  local line_start = arg.line1
  -- End is exclusive hence the `+ 1`.
  local line_end = arg.line2 + 1
```

And we also need to get the path to the current file:

```lua
  local file_path = vim.fn.expand('%:p')
```

*From this point on explanations are git specific, but I'm sure other VCSes have similar features.*

Note that since the current directory might be one or several directories deep relative to the root of the git repository, e.g. `src/`, we need to fix this path, because the git web UI expects a path from the root of the git repository.

The easiest way to do so is using `git ls-files`, e.g. if we are in `./src/` and the file is `main.c`, `git ls-files main.c` returns `./src/main.c`. That's very handy to avoid any complex path manipulations. 

There are many ways in Neovim to call out to a command in a subprocess, here's one of them, to get the output of the command:

```lua
  local cmd_handle = io.popen('git ls-files ' .. file_path)
  local file_path_relative_to_git_root = cmd_handle:read('*a')
  cmd_handle.close()
```

We also need to get the git URL of the remote (assuming there is only one, but it's easy to expand the logic to handle multiple):

```lua
  local cmd_handle = io.popen('git remote get-url origin')
  local git_origin = cmd_handle:read('*a')
  cmd_handle.close()
```

And the last bit of information we need is to get the current commit.
In the past, I just used the current branch name, however since this is a moving target, it meant that when opening the link, the code might be completely different than what it was when giving out the link. Using a fixed commit is thus better (assuming no one force pushes and messes with the history):

```lua
  local cmd_handle = io.popen('git rev-parse HEAD')
  local git_commit = cmd_handle:read('*a')
  cmd_handle.close()
```

Now, we can craft the URL by first extracting the interesting parts of the git remote URL and then tacking on at the end all the URL parameters precising the location.
I assume the git remote URL is a `ssh` URL here, again it's easy to tweak to also handle `https` URL. Also note that this is the part that's hosting provider specific.

Since I am mainly using Azure DevOps (ADO) at the moment this is what I'll show. In ADO, the remote URL looks like this:

```
git@ssh.<hostname>:v3/<organization>/<directory>/<project>
```

And the final URL looks like:

```lua
https://<hostname>/<organization>/<directory>/_git/<project>?<params>
```

We use a Lua pattern to do that using `string.gmatch`. It weirdly returns an iterator yielding only one result containing our matches, we use a `for` loop to do so (perhaps there is an easier way in Lua?):

```lua
  local url = ''
  for host, org, dir, project in string.gmatch(git_origin, 'git@ssh%.([^:]+):v3/([^/]+)/([^/]+)/([^\n]+)') do
    url = 'https://' .. host .. '/' .. org .. '/' .. dir .. '/_git/' .. project .. '?lineStartColumn=1&lineStyle=plain&_a=contents&version=GC' .. git_commit .. '&path=' .. file_path_relative_to_git_root .. '&line=' .. line_start .. '&lineEnd=' .. line_end
    break
  end
```

Finally we stick the result in the system clipboard, and we can even open the url in the default browser using `xdg-open` (on macOS it'll be `open`):

```lua
  vim.fn.setreg('+', url)
  os.execute('xdg-open "' .. url .. '"')
```

And that's it, just 25 lines of Lua, and easy to extend to support more hosting providers (just inspect the hostname).

We can now map the command to our favorite keystroke, for me space + x, for both normal mode (`n`) and visual mode (`v`):

```lua
vim.keymap.set({'v', 'n'}, '<leader>x', ':GitWebUiUrlCopy<CR>')
```
