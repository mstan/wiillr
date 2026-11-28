# SPDX-License-Identifier: GPL-3.0-or-later
"""Consumer-owned product policy; the engine only supplies developer tools."""
from __future__ import annotations
import json
from pathlib import Path
import re

def load(path: Path | None, product: str) -> dict:
    if product == "wiillr" and path is None:
        return {"engine_product": "wiillr", "name": "wiillr", "archive_prefix": "wiillr"}
    if path is None:
        raise ValueError("Application builds require a consumer --product-file")
    path = path.resolve(strict=True)
    value = json.loads(path.read_text(encoding="utf-8"))
    if value.get("engine_product") != product:
        raise ValueError("Product description belongs to another consumer")
    for key in ("engine_product", "archive_prefix"):
        if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]*", value.get(key, "")):
            raise ValueError("Invalid consumer " + key)
    if not value.get("name") or any(c in value["name"] for c in "/\\\r\n"):
        raise ValueError("Invalid consumer name")
    for key in ("cmake", "readme"):
        relative = Path(value[key])
        resolved = (path.parent / relative).resolve(strict=True)
        if relative.is_absolute() or ".." in relative.parts or not resolved.is_relative_to(path.parent):
            raise ValueError("Consumer path escapes product root: " + key)
        value[key] = str(resolved)
    value["product_file"] = str(path)
    return value
