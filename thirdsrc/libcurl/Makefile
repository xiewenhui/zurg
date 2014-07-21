.PHONY: output clean

VERSION=7.30.0

output: clean curl-$(VERSION).tar.bz2 Makefile
	tar -xjf curl-$(VERSION).tar.bz2
	cd curl-$(VERSION) && patch -p1 < ../curl-$(VERSION).patch
	cd curl-$(VERSION) && ./configure --prefix=`pwd`/../output                        \
        --enable-http --disable-crypto-auth --disable-ipv6 --disable-tls-srp          \
        --disable-ftp --disable-file --disable-ldap --disable-ldaps --disable-rtsp    \
        --disable-proxy --disable-dict --disable-telnet --disable-tftp --disable-pop3 \
        --disable-imap --disable-smtp --disable-gopher --disable-manual --without-ssl \
        --without-libidn --without-ca-bundle --without-ca-path --without-zlib         \
        --disable-libcurl-option --enable-verbose --disable-shared --enable-static    \
        --disable-cookies --enable-symbol-hiding --disable-threaded-resolver          \
        --disable-curldebug --enable-optimize --disable-debug --disable-thread        \
        --without-librtmp --without-libssh2 --without-winidn --without-libmetalink    \
        --without-axtls --without-nss  --without-cyassl --disable-dependency-tracking \
        --without-gnutls --without-darwinssl --without-winssl --disable-ntlm-wb       \
        --disable-sspi --disable-largefile --without-polarssl --disable-ares
	cd curl-$(VERSION) && make && make install
	rm -rf ../../third64/libcurl
	mv output ../../third64/libcurl

clean:
	rm -rf curl-$(VERSION)
	rm -rf output
