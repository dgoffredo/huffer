.PHONY: diagrams
diagrams: mary.svg foobar.svg tree.svg

tree.svg: ../tree
	<$< gzip --no-name | ../tree | dot -Tsvg -o $@

%.svg: %.txt ../tree
	<$< ../tree | dot -Tsvg -o $@

../tree: ../tree.cpp
	$(CXX) -Og -o $@ $^
