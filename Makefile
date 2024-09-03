.PHONY: all
all: diagrams/foobar.svg diagrams/mary.svg

huffer: huffer.cpp
	$(CXX) -o $@ --std=c++20 -O2 -Wall -Wextra -pedantic -Werror $^

%.svg: %.txt huffer
	./huffer graph $< | dot -Tsvg >$@
