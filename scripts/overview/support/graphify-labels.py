"""Names the communities of the graphify graph after the folders their code is in.

Run with graphify's own Python by 01-graphify.ps1, after "graphify cluster-only":

    python graphify-labels.py <graphify-out folder>

Without a language model graphify names a community after its most connected node, which is
often a standard header ("vector", "memory") or a class name that several steps share
("OptionsWidget" four times). This names it after the folder most of its code is in, followed
by that node, e.g. "core/filters/deskew/ui · OptionsWidget". Nothing leaves the machine.

Writes into the folder:
  .graphify_labels.json, .sig   the names, with graphify's membership signatures, so the next
                                "graphify cluster-only" keeps them and writes them into the
                                report and graph.html
  COMMUNITIES_BY_FOLDER.md      for each folder the communities its code is in, to find the
                                communities of the part of the code a review looks at
"""

import collections
import json
import posixpath
import sys
from pathlib import Path

from graphify.cluster import community_member_sigs

# A community whose biggest folder holds less of its code than this is named after its two
# biggest folders.
MAJORITY = 0.5
# COMMUNITIES_BY_FOLDER.md lists a community under a folder only from this many nodes on, so
# that one include of a header does not list the community there.
MIN_NODES_IN_FOLDER = 3


def folder_of(source_file: str) -> str:
    folder = posixpath.dirname(source_file.replace("\\", "/"))
    # Nearly all code is in src/; without the prefix the names stay short.
    if folder == "src":
        return "src"
    return folder[4:] if folder.startswith("src/") else folder or "."


def hub_name(node: dict) -> str:
    name = str(node.get("label") or node["id"]).strip()
    return name[:-2] if name.endswith("()") else name


def main(out_dir: Path) -> None:
    graph = json.loads((out_dir / "graph.json").read_text(encoding="utf-8"))
    degree = collections.Counter()
    for link in graph["links"]:
        degree[link["source"]] += 1
        degree[link["target"]] += 1

    members: dict[int, list[dict]] = collections.defaultdict(list)
    for node in graph["nodes"]:
        if node.get("community") is not None:
            members[int(node["community"])].append(node)

    labels: dict[int, str] = {}
    folder_counts: dict[int, collections.Counter] = {}
    for cid, nodes in members.items():
        # Nodes without a file are names the code only uses (std::vector, QString, a header
        # outside the repository); they say nothing about where the community is.
        own = [n for n in nodes if n.get("source_file")]
        counts = collections.Counter(folder_of(n["source_file"]) for n in own)
        folder_counts[cid] = counts
        if not own:
            labels[cid] = f"outside the repository · {hub_name(max(nodes, key=lambda n: (degree[n['id']], n['id'])))}"
            continue
        # Most connected node, ties broken by id so that the names stay the same from run to run.
        hub = max(own, key=lambda n: (degree[n["id"]], n["id"]))
        top = counts.most_common(2)
        if top[0][1] >= MAJORITY * len(own) or len(top) == 1:
            where = top[0][0]
        else:
            where = f"{top[0][0]} + {top[1][0]}" + (" + …" if len(counts) > 2 else "")
        labels[cid] = f"{where} · {hub_name(hub)}"

    communities = {cid: [n["id"] for n in nodes] for cid, nodes in members.items()}
    (out_dir / ".graphify_labels.json").write_text(
        json.dumps({str(k): v for k, v in sorted(labels.items())}, ensure_ascii=False), encoding="utf-8")
    (out_dir / ".graphify_labels.json.sig").write_text(
        json.dumps({str(k): v for k, v in sorted(community_member_sigs(communities).items())}), encoding="utf-8")

    by_folder: dict[str, list[tuple[int, int]]] = collections.defaultdict(list)
    for cid, counts in folder_counts.items():
        for folder, count in counts.items():
            if count >= MIN_NODES_IN_FOLDER:
                by_folder[folder].append((count, cid))
    lines = [
        "# Communities by folder",
        "",
        "For each folder the communities its code is in, the one with most of its nodes first: "
        "community number, nodes of this folder in it / all its nodes, name. The members of a "
        "community are listed under its number in GRAPH_REPORT.md. Communities with fewer than "
        f"{MIN_NODES_IN_FOLDER} nodes of a folder are left out there.",
        "",
    ]
    for folder in sorted(by_folder):
        lines.append(f"## {folder}")
        lines.append("")
        for count, cid in sorted(by_folder[folder], key=lambda e: (-e[0], e[1])):
            lines.append(f"- {cid}: {count}/{len(members[cid])} · {labels[cid]}")
        lines.append("")
    (out_dir / "COMMUNITIES_BY_FOLDER.md").write_text("\n".join(lines), encoding="utf-8")
    print(f"Named {len(labels)} communities after their folders; COMMUNITIES_BY_FOLDER.md written.")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: graphify-labels.py <graphify-out folder>")
    main(Path(sys.argv[1]))
