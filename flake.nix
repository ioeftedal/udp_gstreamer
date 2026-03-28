{
  description = "c project template with Qt6";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-25.11";
    gstreamer.url = "github:uzaaft/gstreamer-nix";
  };

  outputs = { self, nixpkgs, gstreamer, ... }:
    let
      allSystems = [ "x86_64-linux" "aarch64-linux" "aarch64-darwin" ];

      forAllSystems = f:
        nixpkgs.lib.genAttrs allSystems (system:
          f {
            pkgs = nixpkgs.legacyPackages.${system};
            inherit system;
          });

    in {
      packages = forAllSystems ({ pkgs, system }: {
        default = pkgs.stdenv.mkDerivation {
          name = "c-project";
          src = ./.;

          nativeBuildInputs = [ pkgs.cmake ];

          buildPhase = ''
            cmake -B build -S .
            cmake --build build
          '';

          installPhase = ''
            mkdir -p $out
            cp -r build/* $out/
          '';
        };
      });

      devShells = forAllSystems ({ pkgs, system }:
        let
          qtEnv = pkgs.qt6.env "qt6-env-${pkgs.qt6.qtbase.version}" [
            pkgs.qt6.qtbase
            pkgs.qt6.qttools
            pkgs.qt6.qtdeclarative
            pkgs.qt6.qt5compat
            pkgs.qt6.qtwebchannel
            pkgs.qt6.qtpositioning
            pkgs.qt6.qtwebengine
            pkgs.qt6.qtmultimedia
          ];
          gstreamerPackage = gstreamer.packages.${system}.default;
        in {
          default = pkgs.mkShell {
            buildInputs = with pkgs; [
              # Core tooling
              cmake
              pkg-config
              clang
              clang-tools

              # C/C++ libs
              llvmPackages_latest.libllvm
              llvmPackages_latest.libcxx
              stdenv.cc.cc.lib

              # Qt
              qtEnv
              qt6.qtbase
              qt6.wrapQtAppsHook
              # qtcreator

              # GStreamer (pinned via uzaaft/gstreamer-nix)
              gstreamerPackage
            ];

            shellHook = ''
              # Compiler / linker paths
              export LD_LIBRARY_PATH=${pkgs.stdenv.cc.cc.lib}/lib:$LD_LIBRARY_PATH

              # Qt setup
              export QT_PLUGIN_PATH="${qtEnv}/lib/qt-6/plugins"
              export QML_IMPORT_PATH="${qtEnv}/lib/qt-6/qml"
              export QT_QPA_PLATFORM_PLUGIN_PATH="${qtEnv}/lib/qt-6/plugins/platforms"

              # Wayland (Linux only, harmless elsewhere)
              export QT_QPA_PLATFORM=wayland

              # pkg-config
              export PKG_CONFIG_PATH="${qtEnv}/lib/pkgconfig:$PKG_CONFIG_PATH"
            '';
          };
        });
    };
}
