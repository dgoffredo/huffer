.DELETE_ON_ERROR:

DIAGRAMS := mary mississippi foobar tree double-mary double-mississippi double-odd license
DOTS := $(DIAGRAMS:=.dot)
SVGS := $(DIAGRAMS:=.svg)

.PHONY: all
all: $(DOTS) $(SVGS)

tree.dot: ../tree
	<$< gzip --no-name | ../tree >$@

license.dot: ../LICENSE.md ../tree
	<$< ../tree >$@

%.svg: %.dot
	dot -Tsvg -o $@ $^

%.dot: %.txt ../tree
	<$< ../tree >$@

double-%.dot: %.txt ../double-tree
	<$< ../double-tree >$@

../tree: ../tree.cpp
	$(CXX) -Og -o $@ $^

../double-tree: ../tree.cpp
	$(CXX) -DSYMBOL_SIZE=2 -Og -o $@ $^
