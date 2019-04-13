CONTRACT=escrowescrow

all: $(CONTRACT).wasm $(CONTRACT).abi

%.wasm: %.cpp escrowescrow_constants.hpp
	eosio-cpp -I. -o $@ $<

%.abi: %.cpp
	eosio-abigen -contract=$(CONTRACT) --output=$@ $<

clean:
	rm -f $(CONTRACT).wasm $(CONTRACT).abi
