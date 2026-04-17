{
  description = "Endpaper - C++ TUI/GUI Email-to-EPUB Archiver";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    nixpkgs-stable.url = "github:NixOS/nixpkgs/nixos-24.11"; # glibc 2.40
    flake-utils.url = "github:numtide/flake-utils";
    nix2flatpak.url = "github:neobrain/nix2flatpak";
  };

  outputs = { self, nixpkgs, nixpkgs-stable, flake-utils, nix2flatpak }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
        pkgs-stable = import nixpkgs-stable {
          inherit system;
          overlays = [
            (final: prev: {
              # Use ftxui from unstable but build it with stable stdenv to ensure glibc compatibility
              ftxui = prev.ftxui.overrideAttrs (old: {
                src = nixpkgs.legacyPackages.${system}.ftxui.src;
              });
            })
          ];
        };
        kde = pkgs.kdePackages;
        mkFlatpak = nix2flatpak.lib.${system}.mkFlatpak;

        # Kirigami's qmldir files contain 'prefer :/qt/qml/...' directives that
        # force the QML engine to load from QRC resource paths. When KF6::Kirigami
        # is linked statically, those resources exist in the binary. On NixOS we
        # don't link it (it breaks qt6_import_qml_plugins), so we strip the
        # 'prefer' lines to let the engine use the filesystem paths instead.
        kirigamiQml = pkgs.runCommand "kirigami-qml-patched" {} ''
          cp -r ${pkgs.kdePackages.kirigami.unwrapped}/lib/qt-6/qml $out
          chmod -R u+w $out
          find $out -name qmldir -exec sed -i -e '/^prefer /d' -e '/^linktarget /d' {} \;
        '';

        mkEndpaper = { pkgs, withGui ? true, withTui ? true }: 
          let
            kde = pkgs.kdePackages;
            pname = if (withGui && !withTui) then "endpaper-qt" 
                    else if (withTui && !withGui) then "endpaper"
                    else "endpaper-all";
          in
          pkgs.stdenv.mkDerivation {
            inherit pname;
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
              kde.akonadi
              kde.kmime
              kde.akonadi-mime
              kde.kdepim-runtime
              pkgs.ftxui
              pkgs.sqlite
              pkgs.libzip
              pkgs.pugixml
              pkgs.lexbor
              pkgs.curl
              pkgs.nlohmann_json
            ] ++ (with pkgs.lib; optionals withGui [
              kde.qtdeclarative
              kde.qtsvg
              kde.kirigami
              kde.kirigami-addons
              kde.ki18n
              kde.breeze
              pkgs.kdePackages.qqc2-breeze-style
              kde.plasma-integration
            ]);

            cmakeFlags = [
              "-DCMAKE_BUILD_TYPE=Release"
              "-DCMAKE_INSTALL_PREFIX=${placeholder "out"}"
              "-DBUILD_TUI=${if withTui then "ON" else "OFF"}"
              "-DBUILD_GUI=${if withGui then "ON" else "OFF"}"
            ];

            dontWrapQtApps = true;

            postInstall = ''
              # Wrap TUI if built
              if [ -f $out/bin/endpaper ]; then
                wrapProgram $out/bin/endpaper \
                  --prefix QT_PLUGIN_PATH : "${kde.qtbase}/lib/qt-6/plugins"
              fi

              # Wrap GUI if built
              if [ -f $out/bin/endpaper-qt ]; then
                wrapProgram $out/bin/endpaper-qt \
                  --set QML_DISABLE_DISK_CACHE "1" \
                  --prefix QML2_IMPORT_PATH : "${kirigamiQml}" \
                  --prefix QML2_IMPORT_PATH : "${kde.kirigami-addons}/lib/qt-6/qml" \
                  --prefix QML2_IMPORT_PATH : "${pkgs.kdePackages.qqc2-breeze-style}/lib/qt-6/qml" \
                  --prefix QML2_IMPORT_PATH : "${kde.qtdeclarative}/lib/qt-6/qml" \
                  --prefix QT_PLUGIN_PATH : "${kde.qtbase}/lib/qt-6/plugins" \
                  --prefix QT_PLUGIN_PATH : "${kde.plasma-integration}/lib/qt-6/plugins" \
                  --prefix QT_PLUGIN_PATH : "${kde.breeze}/lib/qt-6/plugins" \
                  --prefix XDG_DATA_DIRS : "${kde.kirigami}/share" \
                  --prefix XDG_DATA_DIRS : "${pkgs.kdePackages.breeze-icons}/share" \
                  --prefix XDG_DATA_DIRS : "$out/share" \
                  --set QT_QPA_PLATFORMTHEME "kde" \
                  --set QT_QUICK_CONTROLS_STYLE "org.kde.breeze"
              fi
            '';
          };
      in
      {
        packages = rec {
          endpaper = mkEndpaper { inherit pkgs; withGui = false; withTui = true; };
          endpaper-qt = mkEndpaper { inherit pkgs; withGui = true; withTui = false; };
          all = mkEndpaper { inherit pkgs; withGui = true; withTui = true; };
          default = endpaper;

          flatpak-endpaper = mkFlatpak {
            appId = "page.codeberg.gnuslashdhruv.endpaper";
            package = mkEndpaper { pkgs = pkgs-stable; withGui = false; withTui = true; };
            runtime = "org.kde.Platform/6.10";
            permissions = {
              share = [ "network" ];
              filesystems = [ "home" ];
            };
            skipAbiChecks = true;
          };

          flatpak-endpaper-qt = mkFlatpak {
            appId = "page.codeberg.gnuslashdhruv.endpaper-qt";
            package = mkEndpaper { pkgs = pkgs-stable; withGui = true; withTui = false; };
            runtime = "org.kde.Platform/6.10";
            permissions = {
              share = [ "network" "ipc" ];
              sockets = [ "wayland" "fallback-x11" "session-bus" ];
              filesystems = [ 
                "home"
                "xdg-run/dconf"
                "xdg-config/kdeglobals:ro"
              ];
              talk-names = [ "org.freedesktop.Akonadi" "org.freedesktop.Notifications" ];
              devices = [ "dri" ];
            };
            skipAbiChecks = true;
          };
        };

        devShells.default = pkgs.mkShell {
          inputsFrom = [ self.packages.${system}.all ];

          nativeBuildInputs = [
            pkgs.gdb
            pkgs.valgrind
            pkgs.clang-tools
          ];

          QML_IMPORT_PATH = pkgs.lib.makeSearchPath "lib/qt-6/qml" (with kde; [ qtdeclarative kirigami-addons pkgs.kdePackages.qqc2-breeze-style ]) + ":${kirigamiQml}";
          QML2_IMPORT_PATH = pkgs.lib.makeSearchPath "lib/qt-6/qml" (with kde; [ qtdeclarative kirigami-addons pkgs.kdePackages.qqc2-breeze-style ]) + ":${kirigamiQml}";
          QT_PLUGIN_PATH = pkgs.lib.makeSearchPath "lib/qt-6/plugins" (with kde; [ qtbase akonadi-mime plasma-integration breeze ]);
          XDG_DATA_DIRS = pkgs.lib.makeSearchPath "share" (with kde; [ kirigami breeze-icons ]);
          QT_QPA_PLATFORMTHEME = "kde";
          QT_QUICK_CONTROLS_STYLE = "org.kde.breeze";

          shellHook = ''
            echo "🔧 Endpaper dev shell ready"
            echo "   cmake -B build && cmake --build build"
          '';
        };
      }
    );
}
