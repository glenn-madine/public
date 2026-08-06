# process_sales.awk

# The BEGIN block executes once before processing the text file
BEGIN {
    print "======================================"
    print "      HIGH PERFORMING SALES REPS      "
    print "======================================"
    printf "%-10s %-10s %-10s\n", "NAME", "SALES", "REGION"
    print "--------------------------------------"
    
    # Initialize our total sales variable
    total_sales = 0
}

# This block executes for every line where sales (Column 2) is greater than 1500
$2 > 1500 {
    # Print formatted columns
    printf "%-10s $%-9s %-10s\n", $1, $2, $3
    
    # Add the sales amount to our running total
    total_sales += $2
}

# The END block executes once after all lines have been processed
END {
    print "--------------------------------------"
    print "Total Sales from High Performers: $" total_sales
    print "Total Records Processed: " NR
    print "======================================"
}
