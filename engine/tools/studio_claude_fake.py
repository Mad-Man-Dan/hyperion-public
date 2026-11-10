"""A stand-in for Claude Code that the studio's Claude panel can run in place of it, so a test of
the panel spends nothing (studio_claude_use {program: this file}; tools/studio_test.py).

It takes the arguments the panel gives claude -p, reads the prompt from stdin, and answers in
claude's stream-json events. What it does is written in the prompt after "fake:", steps with "|"
between them:

    set <table> <name> <field> <json value>   studio_set on the studio's channel, as Claude's
    call <command> <json fields>               any studio command, as Claude's
    deny <tool>                                a tool call refused as not given
    say <words>                                Claude's words
    sleep <seconds>                            a wait, for Stop
    exit <code>                                an end without an answer

The channel's port is the one the panel's MCP config gives the hyperion-studio server.
"""
import json
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "control"))
from hyperion_ctl import Client  # noqa: E402

SERVER = "mcp__hyperion-studio__"


def out(event):
    sys.stdout.write(json.dumps(event) + "\n")
    sys.stdout.flush()


def main() -> int:
    args = sys.argv[1:]
    def opt(name):
        return args[args.index(name) + 1] if name in args else ""
    session = opt("--session-id") or opt("--resume")
    config = json.load(open(opt("--mcp-config"), encoding="utf-8"))
    port = int(config["mcpServers"]["hyperion-studio"]["env"]["HYPERION_CONTROL_PORT"])
    prompt = sys.stdin.read()
    steps = prompt.split("fake:", 1)[1].strip().split("|") if "fake:" in prompt else ["say I am the stand-in."]
    t0 = time.time()
    out({"type": "system", "subtype": "init", "session_id": session, "tools": [SERVER + "studio_set"],
         "mcp_servers": [{"name": "hyperion-studio", "status": "connected"}]})
    client = None
    said = ""
    n = 0
    for step in (s.strip() for s in steps):
        if not step:
            continue
        word, _, rest = step.partition(" ")
        n += 1
        tid = "toolu_fake_%d" % n
        if word == "say":
            out({"type": "stream_event", "event": {"type": "content_block_start", "index": 0,
                                                   "content_block": {"type": "text", "text": ""}}})
            for piece in rest.split(" "):
                out({"type": "stream_event", "event": {"type": "content_block_delta", "index": 0,
                                                       "delta": {"type": "text_delta", "text": piece + " "}}})
            out({"type": "assistant", "message": {"content": [{"type": "text", "text": rest}]}})
            said = rest
        elif word == "set":
            table, name, field, value = rest.split(" ", 3)
            value = json.loads(value)
            out({"type": "assistant", "message": {"content": [{"type": "tool_use", "id": tid, "name": SERVER + "studio_set",
                                                               "input": {"table": table, "name": name, "field": field,
                                                                         "value": value}}]}})
            if client is None:
                client = Client(port)
            answer = client.call("studio_set", timeout=10.0, table=table, name=name, field=field, value=value, sender="claude")
            out({"type": "user", "message": {"content": [{"type": "tool_result", "tool_use_id": tid,
                                                          "content": [{"type": "text", "text": json.dumps(answer)}]}]}})
        elif word == "call":
            cmd, _, fields = rest.partition(" ")
            fields = json.loads(fields) if fields.strip() else {}
            out({"type": "assistant", "message": {"content": [{"type": "tool_use", "id": tid, "name": SERVER + cmd,
                                                               "input": fields}]}})
            if client is None:
                client = Client(port)
            answer = client.call(cmd, timeout=10.0, sender="claude", **fields)
            out({"type": "user", "message": {"content": [{"type": "tool_result", "tool_use_id": tid,
                                                          "content": [{"type": "text", "text": json.dumps(answer)}]}]}})
        elif word == "deny":
            out({"type": "assistant", "message": {"content": [{"type": "tool_use", "id": tid, "name": SERVER + rest, "input": {}}]}})
            out({"type": "system", "subtype": "permission_denied", "tool_name": SERVER + rest, "tool_use_id": tid})
            out({"type": "user", "message": {"content": [{"type": "tool_result", "tool_use_id": tid, "is_error": True,
                                                          "content": "Permission to use %s%s has been denied." % (SERVER, rest)}]}})
        elif word == "sleep":
            time.sleep(float(rest))
        elif word == "exit":
            return int(rest)
    out({"type": "result", "subtype": "success", "is_error": False, "duration_ms": int((time.time() - t0) * 1000),
         "total_cost_usd": 0, "result": said, "session_id": session})
    return 0


if __name__ == "__main__":
    sys.exit(main())
