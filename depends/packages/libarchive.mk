package=libarchive
$(package)_version=3.3.2
$(package)_download_path=http://www.libarchive.org/downloads
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_sha256_hash=ed2dbd6954792b2c054ccf8ec4b330a54b85904a80cef477a1c74643ddafa0ce
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
