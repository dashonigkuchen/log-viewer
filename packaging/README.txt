Log Viewer @VERSION@ - portable Windows build
==============================================

A fast native viewer for large log files. Website:
https://github.com/dashonigkuchen/log-viewer

Install
-------
No installer is needed. Extract the zip to any folder and start logviewer.exe.
The program is not code-signed, so Windows SmartScreen may warn on the first start
("More info" > "Run anyway").

Settings are stored in %APPDATA%\LogViewer. For a fully portable setup, create an
empty file named portable.ini next to logviewer.exe; settings are then stored in
this folder instead.

Command line
------------
  logviewer [files...]       open files (as tabs in a running window)
  logviewer --follow FILE    open in follow mode
  logviewer --line N FILE    go to line N
  logviewer --new-instance   open a separate window
  logviewer --help           all options

Privacy
-------
Log Viewer never accesses the network. If it crashes, a small diagnostic file
(minidump, without file contents) is written to %APPDATA%\LogViewer\dumps. It stays
on your computer; you may attach it to a bug report if you like.

Verify the download
-------------------
SHA256SUMS.txt next to the zip on the release page lists its SHA-256 checksum:
  Get-FileHash LogViewer-@VERSION@-windows-x64.zip -Algorithm SHA256

License
-------
MIT, see LICENSE. Third-party software and licenses: THIRD-PARTY-NOTICES.md.
