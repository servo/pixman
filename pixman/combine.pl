$usage = "Usage: combine.pl { 8 | 16 } < combine.inc";

$#ARGV == 0 or die $usage;

# Get the component size.
$size = int($ARGV[0]);
$size == 8 or $size == 16 or die $usage;

$pixel_size = $size * 4;

print "#line 1 \"combine.inc\"\n";
while (<STDIN>) {
    # Add 32/64 suffix to combining function types.
    s/\bCombineFuncC\b/CombineFuncC$pixel_size/;
    s/\bCombineFuncU\b/CombineFuncU$pixel_size/;
    s/\bCombineMaskU\b/CombineMaskU$pixel_size/;
    s/\bFbComposeFunctions\b/FbComposeFunctions$pixel_size/;

    print $_;
}
