.DELETE_ON_ERROR:

.PHONY: diagrams
diagrams: mary.svg mississippi.svg foobar.svg tree.svg double-mary.svg double-mississippi.svg

tree.svg: ../tree
	<$< gzip --no-name | ../tree | dot -Tsvg -o $@

double-%.svg: %.txt ../double-tree
	<$< ../double-tree | dot -Tsvg -o $@

%.svg: %.txt ../tree
	<$< ../tree | dot -Tsvg -o $@

../tree: ../tree.cpp
	$(CXX) -Og -o $@ $^

../double-tree: ../tree.cpp
	$(CXX) -DSYMBOL_SIZE=2 -Og -o $@ $^
