import sys
import urllib.request
from collections import Counter

url, n_requests = sys.argv[1], int(sys.argv[2])

count = Counter()
count_fail = 0

for i in range(n_requests):
    try:
        with urllib.request.urlopen(url) as resp:
            content = resp.read().decode("utf-8").strip()
            count[content] += 1
    except:
        count_fail += 1

for k in count:
    print(f"{k}: actual weight {count[k] / n_requests * 100}%")
print(f"Failed: {count_fail}")
