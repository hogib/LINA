#!/usr/bin/env perl
# SPDX-License-Identifier: GPL-3.0-or-later
# Merges several logs of `lina_ui_spike --bench` into one: every number is replaced by
# the median of that number across the runs (the bench output is line-for-line identical
# in structure). Used because the bench shares the machine with other work.
#   perl tools/median_runs.pl run1.log run2.log run3.log > median.log
use strict;
use warnings;

my @runs;
for my $file (@ARGV) {
    open(my $fh, '<', $file) or die "$file: $!";
    my @lines = <$fh>;
    close($fh);
    push @runs, \@lines;
}
die "need at least one log\n" unless @runs;
my $n = scalar @{ $runs[0] };
for my $r (@runs) {
    die "logs differ in line count\n" unless scalar(@$r) == $n;
}
my $num = qr/(?<![\w.])-?\d+(?:\.\d+)?(?![\w.])/;
for my $i (0 .. $n - 1) {
    my $template = $runs[0][$i];
    my @cols = map { [ $_->[$i] =~ /$num/g ] } @runs;
    my $same = 1;
    for my $c (@cols) { $same = 0 unless scalar(@$c) == scalar(@{ $cols[0] }); }
    if (!$same) { print $template; next; }
    my $k = 0;
    $template =~ s{($num)}{
        my $orig = $1;
        my @v = sort { $a <=> $b } map { $_->[$k] } @cols;
        $k++;
        my $med = @v % 2 ? $v[$#v / 2] : ($v[@v / 2 - 1] + $v[@v / 2]) / 2;
        my $dec = $orig =~ /\.(\d+)/ ? length($1) : 0;
        sprintf("%*.*f", length($orig), $dec, $med);
    }ge;
    print $template;
}
