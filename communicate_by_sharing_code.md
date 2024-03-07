# Communicate by sharing code

This is a grandiloquent title for a small trick that I've been using for years now in every place I worked at. 

Whenever there is a technical discussion, I think it really helps to look at existing code to anchor the debate in reality.

Screen sharing may work at times but I have found a low-tech solution to make the discussion with coworkers really concrete: Share a link to a region of code in the codebase. It's easily shareable and can be used in documentation and PRs as well. 

Every git web UI worth its salt has that feature, let's take Github for example: [https://github.com/gaultier/micro-kotlin/blob/master/class_file.h#L773-L775](https://github.com/gaultier/micro-kotlin/blob/master/class_file.h#L773-L775)

The hurdle is that every hosting provider has its own URL shape to do so and that's not always documented, so there is a tiny bit of reverse-engineering involved. Compare the previous URL with this one: [https://gitlab.com/philigaultier/jvm-bytecode/-/blob/master/class_file.h?ref_type=heads#L125-127](https://gitlab.com/philigaultier/jvm-bytecode/-/blob/master/class_file.h?ref_type=heads#L125-127). It's slightly different.

So to make it easy to share a link to some code with coworkers, I've written a tiny script to craft the URL for me, from my editor. I select a few lines, hit a keystroke, and the URL is now in the clipboard for me to paste it anywhere.

Since I use Neovim and Lua, this is what I'll cover, but I'm sure any editor can do that. Now that I think of it, there should be an extension already for this? Back when I started using this trick I remember searching for one and finding nothing.

This article could also serve as a gentle introduction to using Lua in Neovim. The code is also directly mappable to Vimscript, Vim9 script or anything really.

So first thing first we need to create a user command to invoke this functionality and later map it to a a keystroke:

```lua
vim.api.nvim_create_user_command('GitWebUiUrlCopy', function(arg)
end,
{force=true, range=true, nargs=0, bang=true, desc='Copy to clipboard a URL to a git webui for the current line'})
```

- `force=true` overrides any previous definition which is handy when iterating over the implementation
- `range=true` allows for selecting mutiple lines and calling this command on the line range, but it also works when not selecting anything (in normal mode)
- `nargs=0` means that no argument is passed to the command

We pass a callback to `nvim_create_user_command` which will be called when we invoke the command. For now it does nothing.

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

Note that since the current directory might be several directories deep, e.g. `src/`, we need to fix this path, because the git web UI expects a path from the root of the git repository.

The easiest way to do so is using `git ls-files`, e.g. if we are in `./src/` and the file is `main.c`, `git ls-files main.c` returns `./src/main.c`. That's very handy to avoid any complex path manipulations. 
There are many ways in Neovim to call out to a command in a subprocess, here's one of them, to get the output of the command:

```lua
  local cmd_handle = io.popen('git ls-files ' .. file_path)
  local file_path_relative_to_git_root = cmd_handle:read('*a')
  cmd_handle.close()
```

We also need to get the git url of the remote (assuming there is only one, but it's easy to expand the logic to handle multiple):

```lua
  local cmd_handle = io.popen('git remote get-url origin')
  local git_origin = cmd_handle:read('*a')
  cmd_handle.close()
```

And the last bit of information we need is to get the current commit.
In the past, I just took the current branch name, however since this is a moving target, it meant that when opening the link, the code might be completely different than when giving out the link.

```lua
  local cmd_handle = io.popen('git rev-parse HEAD')
  local git_commit = cmd_handle:read('*a')
  cmd_handle.close()
```
