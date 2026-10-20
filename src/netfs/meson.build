# fleetwm-netfs: mounts FTP / FTPS / SFTP / WebDAV servers as ordinary folders (FUSE) with libcurl and libssh2, so the file manager
# needs no GVfs for them. Optional: built only when libfuse3, libcurl and libssh2 development files are found (option netfs).
fuse3_dep = dependency('fuse3', required: get_option('netfs'))
curl_dep = dependency('libcurl', required: get_option('netfs'))
ssh2_dep = dependency('libssh2', required: get_option('netfs'))

netfs_core_sources = files('parsers.cpp', 'vfs.cpp')

libnetfs_core = static_library(
  'netfs-core',
  netfs_core_sources,
  include_directories: include_directories('.'),
)

libnetfs_core_dep = declare_dependency(
  link_with: libnetfs_core,
  include_directories: include_directories('.'),
)

if fuse3_dep.found() and curl_dep.found() and ssh2_dep.found()
  executable(
    'fleetwm-netfs',
    files('main.cpp', 'backend.cpp', 'curl_backend.cpp', 'sftp_backend.cpp'),
    dependencies: [libnetfs_core_dep, fuse3_dep, curl_dep, ssh2_dep],
    cpp_args: ['-D_FILE_OFFSET_BITS=64'],
    install: true,
  )
  netfs_built = true
else
  netfs_built = false
endif
