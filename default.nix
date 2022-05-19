with import <nixpkgs> {}; rec{
  ebispEnv = pkgs.mkShell {
    name = "emb-lua";
    nativeBuildInputs = [];
    hardeningDisable = [ "all" ];
    buildInputs = [ lua5_3 ];
  };
}
