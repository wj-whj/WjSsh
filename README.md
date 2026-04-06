# WjSsh

WjSsh is a Windows desktop SSH and SFTP client built with C++, Qt 6, and libssh.

It is designed for remote access without requiring any extra agent, daemon, or helper program on the target machine. As long as the remote host already provides SSH and SFTP, WjSsh can connect to it.

## Highlights

- Multi-session SSH workspace with tabbed connections
- Built-in terminal view for interactive shell access
- Integrated SFTP browser with file and directory operations
- Session management with password or private key authentication
- Remote text file preview and editing
- Recursive directory upload and download
- Drag and drop support for SFTP import/export
- Clipboard-based file import/export in the SFTP panel
- Per-connection remote server status bar with CPU, memory, disk, and network information
- Dark and light themes with a custom desktop-style UI

## Main Features

### SSH terminal

- Multiple SSH sessions can stay connected at the same time
- Terminal input happens directly inside the terminal area
- Common shell keys such as `Tab`, `Ctrl+C`, and `Ctrl+L` are supported
- Terminal focus mode and full-screen mode are available for a larger working area

### SFTP file management

- Browse remote directories
- Open parent directory and manually enter a path
- Upload files and directories
- Download files and directories
- Create directories and empty files
- Rename files and directories
- Delete files and directories recursively
- Preview and edit remote text files
- Drag local files into the SFTP panel to upload
- Copy remote items out through drag export or clipboard export

### Session management

- Save, edit, and delete session profiles
- Password authentication
- Private key authentication
- Optional remembered password storage
- Initial remote directory support
- `known_hosts` verification for first-time connections

### Remote status monitoring

- CPU usage
- Memory usage
- Disk usage
- Network throughput

The bottom status bar reflects the currently active remote connection instead of local machine data.

## Project Structure

- [`src`](/D:/wj/CodeX/WjSsh/src): application source files
- [`assets`](/D:/wj/CodeX/WjSsh/assets): icons and visual assets
- [`build_release.ps1`](/D:/wj/CodeX/WjSsh/build_release.ps1): packaging script for the Windows release build
- [`CMakeLists.txt`](/D:/wj/CodeX/WjSsh/CMakeLists.txt): CMake project definition

## Build Requirements

- Windows
- MSYS2 UCRT64
- Qt 6
- libssh
- CMake
- Ninja

## Build

From the project root:

```powershell
.\build_release.ps1
```

The script will:

- configure the release build
- compile `WjSsh.exe`
- run `windeployqt`
- copy required runtime libraries
- generate a portable release folder
- generate a zip package

## Publish to GitHub

You can publish the source project directly to GitHub without requiring local `git` or `gh`.

### Recommended setup

Run this once:

```powershell
.\setup_github_publish.ps1 -RepositoryName WjSsh -Owner wj-whj -Visibility public
```

This setup script will:

- save project publishing defaults to `.github-publish.local.json`
- save your GitHub token in a Windows user-encrypted local file
- prepare this project for one-command publishing later

After that, publish with:

```powershell
.\quick_publish.ps1
```

You can also set a custom commit message:

```powershell
.\quick_publish.ps1 -CommitMessage "Update project files"
```

### Advanced direct publish

If you prefer the lower-level script, use:

```powershell
.\publish_to_github.ps1 -RepositoryName WjSsh -Owner wj-whj -Visibility public
```

The direct publish script will:

- create the repository if it does not exist
- respect the local `.gitignore`
- upload the source tree as a single commit through the GitHub REST API

The token can come from:

- the `-Token` parameter
- the `GITHUB_TOKEN` environment variable
- an interactive secure prompt if neither is provided

Recommended token scope:

- classic personal access token with `repo`

## Output

Release output:

- [`dist/WjSsh`](/D:/wj/CodeX/WjSsh/dist/WjSsh)
- [`dist/WjSsh-win64.zip`](/D:/wj/CodeX/WjSsh/dist/WjSsh-win64.zip)

## Current Notes

- WjSsh uses the remote system's existing SSH and SFTP services. No extra server-side installation is required.
- The terminal implementation is practical for daily shell work, but it is not yet a fully complete VT emulator in every edge case.
- Windows virtual-file drag export is still an area under active refinement for some drag-and-drop targets.

## Tech Stack

- C++20
- Qt 6 Widgets
- libssh
- CMake + Ninja

## Repository Description

Suggested GitHub repository description:

`Modern Windows SSH and SFTP desktop client built with C++, Qt 6, and libssh.`
