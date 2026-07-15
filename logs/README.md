# logs/

droidcli writes a persistent, timestamped log of every session here
(`log.txt`) - the same lines already printed to the console via
`DroidHost::append_app_log`, but durable across restarts and crashes, so you
can see what happened even after the process is gone.

Created automatically on startup if missing. Everything in this directory
except this file is git-ignored - log contents are local runtime data, not
source.
