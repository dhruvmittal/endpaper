{
  description = "Endpaper - C++ TUI Email-to-EPUB Archiver";

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
          pname = "endpaper";
          version = "0.1.0";
          src = ./.;

          nativeBuildInputs = [
            pkgs.cmake
            pkgs.pkg-config
            pkgs.makeWrapper
            pkgs.qt6.wrapQtAppsHook
            kde.extra-cmake-modules
          ];

          buildInputs = [
            kde.qtbase
            kde.qtdeclarative
            kde.qtsvg
            kde.akonadi
            kde.kmime
            kde.akonadi-mime
            kde.kirigami
            kde.kirigami-addons
            kde.breeze
            kde.qqc2-desktop-style
            kde.plasma-integration
            kde.kdepim-runtime   # provides akonadi_serializer_mail plugin
            pkgs.ftxui
            pkgs.sqlite
            pkgs.libzip
            pkgs.pugixml
          ];

          cmakeFlags = [
            "-DCMAKE_BUILD_TYPE=Release"
            "-DCMAKE_INSTALL_PREFIX=${placeholder "out"}"
          ];

          dontWrapQtApps = true;

          postInstall = ''
            for prog in endpaper endpaper-qt; do
              wrapProgram $out/bin/$prog \
                --set QML_DISABLE_DISK_CACHE "1" \
                --prefix QML2_IMPORT_PATH : "${kde.kirigami}/lib/qt-6/qml" \
                --prefix QML2_IMPORT_PATH : "${kde.kirigami-addons}/lib/qt-6/qml" \
                --prefix QML2_IMPORT_PATH : "${kde.qqc2-desktop-style}/lib/qt-6/qml" \
                --prefix QML2_IMPORT_PATH : "${kde.qtdeclarative}/lib/qt-6/qml" \
                --prefix QT_PLUGIN_PATH : "${kde.qtbase}/lib/qt-6/plugins" \
                --prefix QT_PLUGIN_PATH : "${kde.plasma-integration}/lib/qt-6/plugins" \
                --prefix QT_PLUGIN_PATH : "${kde.breeze}/lib/qt-6/plugins" \
                --prefix XDG_DATA_DIRS : "${kde.kirigami}/share" \
                --prefix XDG_DATA_DIRS : "${pkgs.kdePackages.breeze-icons}/share" \
                --prefix XDG_DATA_DIRS : "$out/share" \
                --set QT_QPA_PLATFORMTHEME "kde"
            done
          '';
        };

        devShells.default = pkgs.mkShell {
          inputsFrom = [ self.packages.${system}.default ];

          nativeBuildInputs = [
            pkgs.gdb
            pkgs.valgrind
            pkgs.clang-tools
          ];

          # Ensure QML modules and plugins are found at runtime during development
          # These are picked up by nix develop --command as well
          QML_IMPORT_PATH = pkgs.lib.makeSearchPath "lib/qt-6/qml" (with kde; [ qtdeclarative kirigami kirigami-addons qqc2-desktop-style ]);
          QML2_IMPORT_PATH = pkgs.lib.makeSearchPath "lib/qt-6/qml" (with kde; [ qtdeclarative kirigami kirigami-addons qqc2-desktop-style ]);
          QT_PLUGIN_PATH = pkgs.lib.makeSearchPath "lib/qt-6/plugins" (with kde; [ qtbase akonadi-mime plasma-integration breeze ]);
          XDG_DATA_DIRS = pkgs.lib.makeSearchPath "share" (with kde; [ kirigami breeze-icons ]);
          QT_QPA_PLATFORMTHEME = "kde";

          shellHook = ''
            echo "🔧 Endpaper dev shell ready"
            echo "   cmake -B build && cmake --build build"
          '';
        };
      }
    );
}
