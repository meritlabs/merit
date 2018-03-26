package=libarchive
$(package)_version=3.3.2
$(package)_download_path=http://www.libarchive.org/downloads
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_sha256_hash=ed2dbd6954792b2c054ccf8ec4b330a54b85904a80cef477a1c74643ddafa0ce
$(package)_build_subdir=cmbuild

define $(package)_preprocess_cmds
  mkdir cmbuild
endef

define $(package)_config_cmds
	cmake -DCMAKE_INSTALL_PREFIX:PATH=$(build_prefix) ..
endef

define $(package)_build_cmds
  $(MAKE)
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef
