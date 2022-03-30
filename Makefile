build:
	@dune build

test:
	@dune runtest --force

clean:
	@dune clean

fmt:
	@dune build @fmt

install:
	@dune build @install
	@dune install

uninstall:
	@dune uninstall

.PHONY: build test install uninstall clean
