"""Real MCP server: each instance is scoped to one authenticated C++ connection.

It has no database credentials, public socket, filesystem/shell tools or ability to
choose another canvas/user. The C++ bridge rechecks ACLs and locks for each call.
"""

import json
from mcp.server.lowlevel import Server
from mcp.types import Tool, TextContent

TOOLS = [
    (
        "list_objects",
        "Search readable canvas object summaries; paginate instead of requesting an entire canvas.",
        {
            "query": {"type": "string"},
            "kind": {"type": "string"},
            "offset": {"type": "integer", "minimum": 0},
            "limit": {"type": "integer", "minimum": 1, "maximum": 50},
        },
        [],
    ),
    (
        "get_objects",
        "Read up to 6 objects by ID, including source and revision. Read before editing.",
        {
            "ids": {
                "type": "array",
                "items": {"type": "string"},
                "minItems": 1,
                "maxItems": 6,
            },
            "position_only": {"type": "boolean"},
        },
        ["ids"],
    ),
    (
        "create_objects",
        "Create up to 10 objects. Code objects use kind=text, renderMode=code, text=source, language=python/cpp/javascript/html/css. Coordinates are normalized to the world and snapped to whole world pixels on write; use the provided viewport anchor.",
        {
            "items": {
                "type": "array",
                "minItems": 1,
                "maxItems": 10,
                "items": {"type": "object"},
            }
        },
        ["items"],
    ),
    (
        "patch_objects",
        "Modify up to 10 existing objects using the revision returned by get_objects. Include full updated source in text/code; preserve existing content when appending. Never guess a revision.",
        {
            "changes": {
                "type": "array",
                "minItems": 1,
                "maxItems": 10,
                "items": {
                    "type": "object",
                    "properties": {
                        "id": {"type": "string"},
                        "revision": {"type": "integer"},
                        "patch": {"type": "object"},
                    },
                    "required": ["id", "revision", "patch"],
                    "additionalProperties": False,
                },
            }
        },
        ["changes"],
    ),
    (
        "delete_objects",
        "Delete explicitly requested objects only; available only when the user enables deletion. Supply read revisions.",
        {
            "objects": {
                "type": "array",
                "minItems": 1,
                "maxItems": 10,
                "items": {
                    "type": "object",
                    "properties": {
                        "id": {"type": "string"},
                        "revision": {"type": "integer"},
                    },
                    "required": ["id", "revision"],
                    "additionalProperties": False,
                },
            }
        },
        ["objects"],
    ),
]


def make_server(bridge, allow_delete=False, *, create_only=False):
    server = Server("agora-canvas-mcp")
    allowed = {
        name: (description, properties, required)
        for name, description, properties, required in TOOLS
        if (not create_only or name == "create_objects") and (name != "delete_objects" or allow_delete)
    }

    @server.list_tools()
    async def list_tools():
        return [
            Tool(
                name=name,
                description=description,
                inputSchema={
                    "type": "object",
                    "properties": properties,
                    "required": required,
                    "additionalProperties": False,
                },
            )
            for name, (description, properties, required) in allowed.items()
        ]

    @server.call_tool()
    async def call_tool(name, arguments):
        if name not in allowed:
            raise ValueError("Tool unavailable")
        result = await bridge(name, arguments)
        return [TextContent(type="text", text=json.dumps(result, ensure_ascii=False))]

    return server
