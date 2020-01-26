#!/usr/bin/perl
#
# create a blank Wang Virtual Disk.

$outname = "blank.wvd";

# number of platters
$platters = 7;

# sectors per platter (make sure it is a multiple of 64 for 2280)
$plat_sectors = int(((65536-64) + 63) / 64) * 64;
die if $plat_sectors >= 65536;

# create the disk image.
# first, put out the header block, which has this format:
#    bytes  0-  4: "WANG\0"
#    bytes  5    : write format version
#    bytes  6    : read format version
#    bytes  7    : write protect
#    bytes  8-  9: number of sectors
#    byte  10    : disk type
#    bytes 11- 15: unused
#    bytes 16-255: disk label

$label = "Your label here.";

open(WVD, ">$outname") || die "Can't open file $outname for writing";
binmode(WVD);

$header  = "WANG";		# 0-3: magic tag
$header .= pack("x");		# 4: null byte
$header .= pack("C", 0);	# 5: write format version
$header .= pack("C", 0);	# 6: read format version
$header .= pack("C", 0);	# 7: write protect
$header .= pack("CC", 		# 8-9: number of sectors per platter, ls first
		$plat_sectors%256,
		$plat_sectors/256);
$header .= pack("C", 3);	# 10: disktype (0=5.25", 1=8", 2=2260, 3=2280)
$header .= pack("C", $platters-1); # 11: number of platters minus 1
$header .= pack("x4");		# 12-15: zero bytes
$header .= $label;
$header .= "\0" x (256-length($header));

print WVD $header;

$zero_data = pack("x256");
foreach my $plat (1 .. $platters) {
    foreach my $sect (1 .. $plat_sectors) {
	print WVD $zero_data;
    }
}

close(WVD);

print "Everything went OK\n";
