SHELL := /bin/bash

MAINNET_NODE_URL = https://eos.greymass.com
MAINNET_ACCOUNT_NAME = scrap
TESTNET_NODE_URL = https://jungle4.greymass.com
TESTNET_ACCOUNT_NAME = scrap.gm
DEVNET_NODE_URL = https://jungle4.greymass.com
DEVNET_ACCOUNT_NAME = scrap.gm
CONTRACT_NAME = eosio.token

build: | build/dir
	cdt-cpp -abigen -abigen_output=build/${CONTRACT_NAME}.abi -o build/${CONTRACT_NAME}.wasm src/${CONTRACT_NAME}.cpp -R src -I include -D DEBUG

build/debug: | build/dir
	cdt-cpp -abigen -abigen_output=build/${CONTRACT_NAME}.abi -o build/${CONTRACT_NAME}.wasm src/${CONTRACT_NAME}.cpp -R src -I include -D DEBUG

build/production: | build/dir
	cdt-cpp -abigen -abigen_output=build/${CONTRACT_NAME}.abi -o build/${CONTRACT_NAME}.wasm src/${CONTRACT_NAME}.cpp -R src -I include

build/dir:
	mkdir -p build

clean:
	rm -rf build

devnet: build/debug
	cleos -u $(DEVNET_NODE_URL) set contract $(DEVNET_ACCOUNT_NAME) \
		build/ ${CONTRACT_NAME}.wasm ${CONTRACT_NAME}.abi

testnet: build/debug
	cleos -u $(TESTNET_NODE_URL) set contract $(TESTNET_ACCOUNT_NAME) \
		build/ ${CONTRACT_NAME}.wasm ${CONTRACT_NAME}.abi

mainnet: build/production
	cleos -u $(MAINNET_NODE_URL) set contract $(MAINNET_ACCOUNT_NAME) \
		build/ ${CONTRACT_NAME}.wasm ${CONTRACT_NAME}.abi

drops/include:
	cp -R ../epoch/include/drops ./include
	cp -R ../epoch/include/epoch.drops ./include
	cp -R ../epoch/include/eosio.system ./include

.PHONY: check
check: cppcheck

.PHONY: cppcheck
cppcheck:
	clang-format --dry-run --Werror src/*.cpp include/${CONTRACT_NAME}/*.hpp

.PHONY: format
format: cppformat

.PHONY: cppformat
cppformat:
	clang-format -i src/*.cpp include/${CONTRACT_NAME}/*.hpp

.PHONY: distclean
distclean: clean
	rm -rf node_modules/

.PHONY: dev/enable
dev/enable:
	cleos -u $(DEVNET_NODE_URL) push action $(DEVNET_ACCOUNT_NAME) create '{"issuer": "scrap.gm", "maximum_supply": "1000000000 SCRAP"}' -p $(DEVNET_ACCOUNT_NAME)@active

.PHONY: testnet/enable
testnet/enable:
	cleos -u $(TESTNET_NODE_URL) push action $(TESTNET_ACCOUNT_NAME) create '{"issuer": "scrap.gm", "maximum_supply": "1000000000 SCRAP"}' -p $(TESTNET_ACCOUNT_NAME)@active

.PHONY: testnet/destroy
testnet/destroy:
	cleos -u $(TESTNET_NODE_URL) push action $(TESTNET_ACCOUNT_NAME) destroy '{"issuer": "scrap.gm", "maximum_supply": "1000000000 SCRAP"}' -p $(TESTNET_ACCOUNT_NAME)@active

.PHONY: mainnet/enable
mainnet/enable:
	cleos -u $(MAINNET_NODE_URL) push action $(MAINNET_ACCOUNT_NAME) create '{"issuer": "scrap", "maximum_supply": "1000000000 SCRAP"}' -p $(MAINNET_ACCOUNT_NAME)@active
