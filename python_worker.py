import sys, json
from sentence_transformers import SentenceTransformer

model = SentenceTransformer("all-MiniLM-L6-v2")

# Signal readiness so C++ knows the (slow) model load finished
print(json.dumps({"status": "ready"}), flush=True)

for line in sys.stdin:
    line = line.strip()
    if not line:
        continue
    try:
        request = json.loads(line)
        embedding = model.encode(request["text"])
        response = {"embedding": embedding.tolist()}
    except Exception as e:
        response = {"error": str(e)}
    print(json.dumps(response), flush=True)