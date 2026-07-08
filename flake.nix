{
  description = "Test program for my libc";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";

    # my-libc.url = "github:iKramp/libc";
    my-libc.url = "git+file:///home/nejc/dev/libc";
  };

  outputs =
    { self, nixpkgs, flake-utils, my-libc, ... }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = import nixpkgs {
          inherit system;
        };

        libc = my-libc.packages.${system}.default;
      in
      {
        devShells.default = pkgs.mkShell {
          buildInputs = with pkgs; [
            gdb
            nasm
            xorriso
            gcc
            gnumake
            bear

            libc
          ];

          shellHook = ''
            export MY_LIBC="${libc}"

            export CFLAGS="-nostdinc -I$MY_LIBC/include $CFLAGS"
            export LDFLAGS="-L$MY_LIBC/lib $LDFLAGS"

            exec zsh
          '';
        };
      }
    );
}
