# Host unit tests for Crypto TKey S3 firmware modules (no device needed).
#   make -C tests/host        build and run every test_*.cpp
SRC      := ../../src
CXX      ?= g++
CXXFLAGS := -std=gnu++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer \
            -Wno-unused-parameter -Istubs -I$(SRC) -DHOST_TEST
STUBS    := stubs/hoststub.cpp
MBEDTLS  := .deps/mbedtls
CXXFLAGS += -I$(MBEDTLS)/include -DMBEDTLS_ALLOW_PRIVATE_ACCESS
LDLIBS   := $(MBEDTLS)/library/libmbedx509.a $(MBEDTLS)/library/libmbedcrypto.a

# Arduino libraries the firmware uses (rweather Crypto, micro-ecc), built once for the host
CRYPTO   := $(HOME)/Arduino/libraries/Crypto/src
UECC     := $(HOME)/Arduino/libraries/micro-ecc
CXXFLAGS += -I$(CRYPTO) -I$(UECC)
LIBOBJS  := $(patsubst %,build/lib/%.o,Crypto Hash KeccakCore SHA3 SHA256 SHA512 Ed25519 Curve25519 BigNumberUtil RNG NoiseSource ChaCha Cipher BLAKE2b) build/lib/uECC.o

# test name -> firmware sources it links
DEPS_test_cbor        := $(SRC)/cbor_codec.cpp
DEPS_test_button      := $(SRC)/button_cadence.cpp
DEPS_test_fido_store  := $(SRC)/fido_store.cpp
DEPS_test_seed_gen    := $(SRC)/seed_gen.cpp
DEPS_test_sd_vault    := $(SRC)/sd_vault.cpp $(SRC)/duress_wipe.cpp $(SRC)/crypto_p256.cpp $(SRC)/nvs_backup.cpp
DEPS_test_emergency_policy := $(SRC)/emergency_policy.cpp
DEPS_test_psbt_core   := $(SRC)/psbt_core.cpp $(SRC)/bip32_engine.cpp $(LIBOBJS)
DEPS_test_evm_decoder := $(SRC)/evm_decoder.cpp $(SRC)/bip32_engine.cpp $(LIBOBJS)
DEPS_test_rgb_status  := $(SRC)/rgb_status.cpp $(SRC)/ui_theme.cpp
DEPS_test_ui_theme    := $(SRC)/ui_theme.cpp $(SRC)/rgb_status.cpp
DEPS_test_wifi_manager := $(SRC)/wifi_manager.cpp
DEPS_test_wallet_families := $(SRC)/wallet_families.cpp $(SRC)/bip32_engine.cpp $(LIBOBJS)
DEPS_test_xpub_export := $(SRC)/xpub_export.cpp $(SRC)/bip32_engine.cpp $(LIBOBJS)
DEPS_test_nvs_backup  := $(SRC)/nvs_backup.cpp
DEPS_test_home_screen := $(SRC)/ui_theme.cpp $(SRC)/rgb_status.cpp
DEPS_test_pin_vault   := $(SRC)/pin_vault.cpp $(SRC)/crypto_p256.cpp
# lets the test look at NVS in the middle of the PIN hash (a simulated power cut)
LDFLAGS_test_pin_vault := -Wl,--wrap=mbedtls_pkcs5_pbkdf2_hmac_ext

TESTS := $(patsubst %.cpp,build/%,$(wildcard test_*.cpp))

# Every host-compilable header must compile on its own (catches a header that relies on
# another being included first; that broke a firmware build on 2026-09-25)
SELFCONTAINED := ui_theme.h ui_engine.h cbor_codec.h fido_store.h pin_vault.h emergency_policy.h \
                 psbt_core.h nvs_backup.h evm_decoder.h button_cadence.h seed_gen.h device_clock.h
headers-check:
	@for h in $(SELFCONTAINED); do printf '#include "%s"\n' $$h | \
	  $(CXX) -std=gnu++17 -fsyntax-only -Wno-unused-parameter -Istubs -I$(SRC) -I$(MBEDTLS)/include \
	  -I$(CRYPTO) -DHOST_TEST -x c++ - || { echo "header $$h is not self-contained"; exit 1; }; done

all: headers-check $(MBEDTLS)/library/libmbedcrypto.a $(TESTS)
	@fail=0; for t in $(TESTS); do ./$$t || fail=1; done; exit $$fail

.SECONDARY: $(LIBOBJS)
build/lib/%.o: $(CRYPTO)/%.cpp
	@mkdir -p build/lib
	$(CXX) -std=gnu++17 -O1 -Istubs -I$(CRYPTO) -Wno-unused-parameter -c -o $@ $<

build/lib/uECC.o: $(UECC)/uECC.c
	@mkdir -p build/lib
	$(CC) -O1 -I$(UECC) -c -o $@ $<

$(MBEDTLS)/library/libmbedcrypto.a:
	./fetch_mbedtls.sh

# Rebuild when the test, the firmware sources it links, or any header they can include changes
.SECONDEXPANSION:
HEADERS  := $(wildcard stubs/*.h) $(wildcard $(SRC)/*.h) $(wildcard *.h)
build/%: %.cpp $(STUBS) $$(DEPS_$$*) $(HEADERS) $(MBEDTLS)/library/libmbedcrypto.a
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $< $(STUBS) $(DEPS_$*) $(LDLIBS) $(LDFLAGS_$*)

clean:
	rm -rf build   # keeps .deps (mbedTLS); delete it by hand to rebuild
.PHONY: all clean headers-check
