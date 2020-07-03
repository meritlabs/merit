package=libarchive
$(package)_version=3.3.3
$(package)_download_path=http://www.libarchive.org/downloads
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_sha512_hash=9d12b47d6976efa9f98e62c25d8b85fd745d4e9ca7b7e6d36bfe095dfe5c4db017d4e785d110f3758f5938dad6f1a1b009267fd7e82cb7212e93e1aea237bab7
$(package)_patches=libarchive-1-fixes.patch
$(package)_dependencies=zlib

define $(package)_preprocess_cmds
  patch -p1 < $($(package)_patch_dir)/libarchive-1-fixes.patch
endef

define $(package)_set_vars
  $(package)_config_opts=--disable-bsdtar --disable-bsdcpio --disable-bsdcat --disable-xml2 --without-cng --with-nettle
  $(package)_config_opts_release=--disable-debug-mode
  $(package)_config_opts_linux=--with-pic
endef

define $(package)_config_cmds
  $($(package)_autoconf)
endef

define $(package)_build_cmds
  $(MAKE)
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef
