{
  description = "Mail2Book - C++ TUI Email-to-EPUB Archiver";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
        kde = pkgs.kdePackages;
      in
      {
        packages.default = pkgs.stdenv.mkDerivation {
          pname = "mail2book";
          version = "0.1.0";
          src = ./.;

          nativeBuildInputs = [
            pkgs.cmake
            pkgs.pkg-config
            pkgs.qt6.wrapQtAppsHook
            kde.extra-cmake-modules
          ];

          buildInputs = [
            pkgs.qt6.qtbase
            kde.akonadi
            kde.kmime
            kde.akonadi-mime
            pkgs.ftxui
            pkgs.sqlite
            pkgs.libzip
            pkgs.pugixml
          ];

          cmakeFlags = [
            "-DCMAKE_BUILD_TYPE=Release"
          ];
        };

        devShells.default = pkgs.mkShell {
          nativeBuildInputs = [
            pkgs.cmake
            pkgs.pkg-config
            pkgs.qt6.wrapQtAppsHook
            kde.extra-cmake-modules
            pkgs.gdb
            pkgs.valgrind
            pkgs.clang-tools
          ];

          buildInputs = [
            pkgs.qt6.qtbase
            kde.akonadi
            kde.kmime
            kde.akonadi-mime
            kde.kdepim-runtime   # provides akonadi_serializer_mail plugin
            pkgs.ftxui
            pkgs.sqlite
            pkgs.libzip
            pkgs.pugixml
          ];

          shellHook = ''
            # Build CMAKE_PREFIX_PATH from all inputs that have cmake configs
            for p in $buildInputs $nativeBuildInputs; do
              if [ -d "$p/lib/cmake" ] || [ -d "$p/share/ECM" ] || [ -d "$p/share/cmake" ]; then
                export CMAKE_PREFIX_PATH="$p''${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
              fi
            done

            # Set QT_PLUGIN_PATH so Akonadi can find serializer plugins
            # Include plugins from buildInputs
            for p in $buildInputs; do
              if [ -d "$p/lib/qt-6/plugins" ]; then
                export QT_PLUGIN_PATH="$p/lib/qt-6/plugins''${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
              fi
            done
            # Include the NixOS system plugin path (has akonadi_serializer_mail.so)
            sysPath="/run/current-system/sw/lib/qt-6/plugins"
            if [ -d "$sysPath" ]; then
              export QT_PLUGIN_PATH="$sysPath''${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
            fi
            # Also check the system-path in the nix store
            for sp in /nix/store/*-system-path/lib/qt-6/plugins; do
              if [ -d "$sp" ]; then
                export QT_PLUGIN_PATH="$sp''${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
                break
              fi
            done

            echo "🔧 Mail2Book dev shell ready"
            echo "   cmake -B build && cmake --build build"
          '';
        };
      }
    );
}
