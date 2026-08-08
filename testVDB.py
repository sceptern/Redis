import argparse
import sys
import time

try:
    import redis
except ImportError:
    print("This script requires the 'redis' package: pip install redis")
    sys.exit(1)


# ------------------------------------------------------------------
# tiny test framework: no pytest dependency, just pass/fail + summary
# ------------------------------------------------------------------

PASS = 0
FAIL = 0
FAILURES = []


def check(name, cond, detail=""):
    global PASS, FAIL
    if cond:
        PASS += 1
        print(f"  [PASS] {name}")
    else:
        FAIL += 1
        FAILURES.append(name)
        print(f"  [FAIL] {name}  {detail}")


def section(title):
    print(f"\n=== {title} ===")


def unique_key(prefix):
    # avoid collisions across repeated test runs against a live server
    return f"{prefix}_{int(time.time() * 1000)}"


# ------------------------------------------------------------------
# tests
# ------------------------------------------------------------------

def test_ping(r):
    section("PING")
    # redis-py's PING callback normalizes the reply to a bool, not the raw
    # "PONG" string, regardless of whether the server sent a simple string
    # or (like ours) a bulk string reply.
    check("PING returns truthy success", r.execute_command("PING") is True)


def test_vset_basic(r):
    section("VSET basic")
    text = unique_key("dogs are great pets")

    resp = r.execute_command("VSET", text)
    check("VSET returns OK", resp == "OK", f"got {resp!r}")

    # re-set the same text: should succeed (refresh embedding), not error
    resp2 = r.execute_command("VSET", text)
    check("VSET on existing key still returns OK", resp2 == "OK", f"got {resp2!r}")

    return text


def test_vset_wrong_args(r):
    section("VSET argument validation")
    try:
        r.execute_command("VSET")
        check("VSET with no args errors", False, "did not raise")
    except redis.exceptions.ResponseError as e:
        check("VSET with no args errors", True, str(e))

    try:
        r.execute_command("VSET", "a", "b")
        check("VSET with 2 args errors", False, "did not raise")
    except redis.exceptions.ResponseError as e:
        check("VSET with 2 args errors", True, str(e))


def test_vadd_basic(r):
    section("VADD basic")
    doc = unique_key("animal_facts.pdf")

    resp = r.execute_command("VADD", doc, "chunk1", "dogs are loyal companions")
    check("VADD creates doc + chunk, returns OK", resp == "OK", f"got {resp!r}")

    resp2 = r.execute_command("VADD", doc, "chunk2", "cats are independent")
    check("VADD adds a second chunk to same doc", resp2 == "OK", f"got {resp2!r}")

    return doc


def test_vadd_upsert(r):
    section("VADD upsert (same chunk_name overwrites)")
    doc = unique_key("upsert_doc.pdf")

    r.execute_command("VADD", doc, "c1", "original text about birds")
    results_before = r.execute_command("VSEARCH", doc, "birds", 5)
    check("chunk c1 present before update", len(results_before) == 3,
          f"got {len(results_before)} elements: {results_before}")

    # overwrite chunk c1 with new text
    resp = r.execute_command("VADD", doc, "c1", "completely different text about rockets")
    check("VADD on existing chunk_name returns OK", resp == "OK", f"got {resp!r}")

    results_after = r.execute_command("VSEARCH", doc, "rockets", 5)
    check("only one chunk exists after upsert (no duplicate)",
          len(results_after) == 3, f"got {len(results_after)} elements: {results_after}")
    check("upserted chunk reflects new text",
          results_after[1] == "completely different text about rockets",
          f"got {results_after}")


def test_vadd_wrong_args(r):
    section("VADD argument validation")
    try:
        r.execute_command("VADD", "doc_only")
        check("VADD with 1 arg errors", False, "did not raise")
    except redis.exceptions.ResponseError as e:
        check("VADD with 1 arg errors", True, str(e))

    try:
        r.execute_command("VADD", "doc", "chunk", "text", "extra")
        check("VADD with 4 args (5 incl. cmd) errors", False, "did not raise")
    except redis.exceptions.ResponseError as e:
        check("VADD with 4 args (5 incl. cmd) errors", True, str(e))


def test_type_conflicts(r):
    section("Type conflicts between VSET and VADD")

    # VSET a key, then try VADD on the same key as a doc name
    vset_key = unique_key("just a plain fact")
    r.execute_command("VSET", vset_key)
    try:
        r.execute_command("VADD", vset_key, "chunk1", "some text")
        check("VADD on a VSET key errors", False, "did not raise")
    except redis.exceptions.ResponseError as e:
        check("VADD on a VSET key errors", True, str(e))

    # VADD a doc, then try VSET on the same key
    doc_key = unique_key("real_doc.pdf")
    r.execute_command("VADD", doc_key, "chunk1", "some doc content")
    try:
        r.execute_command("VSET", doc_key)
        check("VSET on a VADD doc key errors", False, "did not raise")
    except redis.exceptions.ResponseError as e:
        check("VSET on a VADD doc key errors", True, str(e))


def test_vdel(r):
    section("VDEL")

    # delete a VSET key
    vset_key = unique_key("deletable fact")
    r.execute_command("VSET", vset_key)
    resp = r.execute_command("VDEL", vset_key)
    check("VDEL on existing VSET key returns 1", resp == 1, f"got {resp!r}")

    resp2 = r.execute_command("VDEL", vset_key)
    check("VDEL on already-deleted key returns 0", resp2 == 0, f"got {resp2!r}")

    # delete a VADD doc, verify chunks go with it (cascade)
    doc_key = unique_key("cascade_doc.pdf")
    r.execute_command("VADD", doc_key, "c1", "chunk one text")
    r.execute_command("VADD", doc_key, "c2", "chunk two text")

    resp3 = r.execute_command("VDEL", doc_key)
    check("VDEL on doc key returns 1", resp3 == 1, f"got {resp3!r}")

    results = r.execute_command("VSEARCH", doc_key, "chunk", 5)
    check("doc-scoped VSEARCH after VDEL returns nothing (doc gone)",
          len(results) == 0, f"got {results}")

    resp4 = r.execute_command("VDEL", "nonexistent_key_xyz")
    check("VDEL on nonexistent key returns 0", resp4 == 0, f"got {resp4!r}")


def test_vsearch_global(r):
    section("VSEARCH global (VSET-only, must not leak VADD chunks)")

    fact_a = unique_key("elephants are the largest land mammals")
    fact_b = unique_key("giraffes have long necks")
    r.execute_command("VSET", fact_a)
    r.execute_command("VSET", fact_b)

    doc = unique_key("wild_animals.pdf")
    r.execute_command("VADD", doc, "c1", "lions are apex predators in africa")

    results = r.execute_command("VSEARCH", "large land animals", 10)
    check("global VSEARCH returns pairs of [text, score]",
          len(results) % 2 == 0, f"got {len(results)} elements")

    texts = results[0::2]
    check("global VSEARCH includes VSET facts", fact_a in texts or fact_b in texts,
          f"texts: {texts}")
    check("global VSEARCH does NOT include VADD chunk text",
          "lions are apex predators in africa" not in texts,
          f"texts: {texts}")


def test_vsearch_scoped(r):
    section("VSEARCH scoped to a doc_name")

    doc = unique_key("scoped_search_doc.pdf")
    r.execute_command("VADD", doc, "intro", "this document is about space exploration")
    r.execute_command("VADD", doc, "body", "rockets use liquid fuel to reach orbit")
    r.execute_command("VADD", doc, "outro", "the future of space travel is promising")

    # also add an unrelated VSET fact that should never appear in scoped results
    unrelated = unique_key("bananas are yellow")
    r.execute_command("VSET", unrelated)

    results = r.execute_command("VSEARCH", doc, "rockets and fuel", 10)
    check("scoped VSEARCH returns triples [chunk_name, text, score]",
          len(results) % 3 == 0, f"got {len(results)} elements: {results}")

    chunk_names = results[0::3]
    texts = results[1::3]
    check("scoped VSEARCH returns exactly the doc's 3 chunks",
          sorted(chunk_names) == ["body", "intro", "outro"],
          f"got chunk_names: {chunk_names}")
    check("scoped VSEARCH does not leak unrelated VSET facts",
          unrelated not in texts, f"texts: {texts}")
    check("closest match to 'rockets and fuel' is the 'body' chunk",
          chunk_names[0] == "body", f"got order: {chunk_names}")


def test_vsearch_scoped_nonexistent_doc(r):
    section("VSEARCH scoped to a doc that doesn't exist")
    results = r.execute_command("VSEARCH", "no_such_doc_xyz", "anything", 5)
    check("scoped VSEARCH on missing doc returns empty array",
          results == [], f"got {results}")


def test_vsearch_k_limit(r):
    section("VSEARCH respects k")

    doc = unique_key("many_chunks.pdf")
    for i in range(6):
        r.execute_command("VADD", doc, f"c{i}", f"this is chunk number {i} about testing")

    results = r.execute_command("VSEARCH", doc, "testing chunk", 2)
    check("scoped VSEARCH with k=2 returns at most 2 chunks",
          len(results) // 3 <= 2, f"got {len(results)} elements: {results}")

    results_global = r.execute_command("VSEARCH", "testing", 1)
    check("global VSEARCH with k=1 returns at most 1 result",
          len(results_global) // 2 <= 1, f"got {results_global}")


def test_vsearch_wrong_args(r):
    section("VSEARCH argument validation")
    try:
        r.execute_command("VSEARCH")
        check("VSEARCH with no args errors", False, "did not raise")
    except redis.exceptions.ResponseError as e:
        check("VSEARCH with no args errors", True, str(e))

    try:
        r.execute_command("VSEARCH", "only_one_arg")
        check("VSEARCH with 1 arg errors", False, "did not raise")
    except redis.exceptions.ResponseError as e:
        check("VSEARCH with 1 arg errors", True, str(e))

    try:
        r.execute_command("VSEARCH", "a", "b", "c", "d")
        check("VSEARCH with 5 args errors", False, "did not raise")
    except redis.exceptions.ResponseError as e:
        check("VSEARCH with 5 args errors", True, str(e))

    try:
        r.execute_command("VSEARCH", "some text", "not_a_number")
        check("VSEARCH with non-integer k errors", False, "did not raise")
    except redis.exceptions.ResponseError as e:
        check("VSEARCH with non-integer k errors", True, str(e))


# ------------------------------------------------------------------
# main
# ------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="Test suite for VDB commands")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=6379)
    args = parser.parse_args()

    r = redis.Redis(host=args.host, port=args.port, decode_responses=True, protocol=2)

    try:
        r.execute_command("PING")
    except Exception as e:
        print(f"Could not connect to {args.host}:{args.port} — {e}")
        sys.exit(1)

    test_ping(r)
    test_vset_basic(r)
    test_vset_wrong_args(r)
    test_vadd_basic(r)
    test_vadd_upsert(r)
    test_vadd_wrong_args(r)
    test_type_conflicts(r)
    test_vdel(r)
    test_vsearch_global(r)
    test_vsearch_scoped(r)
    test_vsearch_scoped_nonexistent_doc(r)
    test_vsearch_k_limit(r)
    test_vsearch_wrong_args(r)

    print(f"\n{'=' * 50}")
    print(f"RESULTS: {PASS} passed, {FAIL} failed")
    if FAILURES:
        print("Failed tests:")
        for name in FAILURES:
            print(f"  - {name}")
    print("=" * 50)

    sys.exit(1 if FAIL else 0)


if __name__ == "__main__":
    main()