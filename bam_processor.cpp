#include <fstream>
#include <iostream>
#include <sstream>
#include <stdlib.h>

#include "bam_processor.h"
#include "alignment_filters.h"
#include "error.h"
#include "seqio.h"
#include "stringops.h"

void BamProcessor::read_and_filter_reads(BamTools::BamMultiReader& reader, std::string& chrom_seq, 
					 std::vector<Region>::iterator region_iter, std::map<std::string, std::string>& file_read_groups,
					 std::vector<std::string>& rg_names, std::vector< std::vector<BamTools::BamAlignment> >& alignments_by_rg, BamTools::BamWriter& bam_writer){
  std::vector<BamTools::BamAlignment> region_alignments;
  int read_count = 0;
  int diff_chrom_mate = 0, unmapped_mate = 0, not_spanning = 0; // Counts for filters that are always applied
  int insert_size = 0, multimapped = 0, flank_len = 0, bp_before_indel = 0, end_match_window = 0, num_end_matches = 0; // Counts for filters that are user-controlled
  BamTools::BamAlignment alignment;
  while (reader.GetNextAlignment(alignment)){
    read_count++;
    
    // Ignore read if its mate pair chromosome doesn't match
    if (alignment.RefID != alignment.MateRefID){
      diff_chrom_mate++;
      continue;
    }
    // Ignore read if its mate pair is unmapped
    if (alignment.InsertSize == 0){
      unmapped_mate++;
      continue;
    }
    // Ignore read if it does not span the STR
    if (alignment.Position > region_iter->start() || alignment.GetEndPosition() < region_iter->stop()){
      not_spanning++;
      continue;
    }
    // Ignore read if its mate pair distance exceeds the threshold
    if (abs(alignment.InsertSize) > MAX_MATE_DIST){
      insert_size++;
      continue;
    }
    // Ignore read if multimapper and filter specified
    if (REMOVE_MULTIMAPPERS && alignment.HasTag("XA")){
      multimapped++;
      continue;
    }
    // Ignore read if it has insufficient flanking bases on either side of the STR
    if (alignment.Position > (region_iter->start()-MIN_FLANK) || alignment.GetEndPosition() < (region_iter->stop()+MIN_FLANK)){
      flank_len++;
      continue;
    }
    // Ignore read if there is an indel within the first MIN_BP_BEFORE_INDEL bps from each end
    if (MIN_BP_BEFORE_INDEL > 0){
      std::pair<int, int> num_bps = AlignmentFilters::GetEndDistToIndel(alignment);
      if ((num_bps.first != -1 && num_bps.first < MIN_BP_BEFORE_INDEL) || (num_bps.second != -1 && num_bps.second < MIN_BP_BEFORE_INDEL)){
	bp_before_indel++;
	continue;
      }
    }
    // Ignore read if there is another location within MAXIMAL_END_MATCH_WINDOW bp for which it has a longer end match
    if (MAXIMAL_END_MATCH_WINDOW > 0){
      bool maximum_end_matches = AlignmentFilters::HasLargestEndMatches(alignment, chrom_seq, 0, MAXIMAL_END_MATCH_WINDOW, MAXIMAL_END_MATCH_WINDOW);
      if (!maximum_end_matches){
	end_match_window++;
	continue;
      }
    }
    // Ignore read if it doesn't match perfectly for at least MIN_READ_END_MATCH bases on each end
    if (MIN_READ_END_MATCH > 0){
      std::pair<int,int> match_lens = AlignmentFilters::GetNumEndMatches(alignment, chrom_seq, 0);
      if (match_lens.first < MIN_READ_END_MATCH || match_lens.second < MIN_READ_END_MATCH){ 
	num_end_matches++;
	continue;
      }
    }
    region_alignments.push_back(alignment);
  }
  std::cerr << read_count << " reads overlapped region, of which " 
	    << "\n\t" << diff_chrom_mate  << " had mates on a different chromosome"
	    << "\n\t" << unmapped_mate    << " had unmapped mates"
	    << "\n\t" << not_spanning     << " did not span the STR"
	    << "\n\t" << insert_size      << " failed the insert size filter"      
	    << "\n\t" << multimapped      << " were removed due to multimapping"
	    << "\n\t" << flank_len        << " had too bps in one or more flanks"
	    << "\n\t" << bp_before_indel  << " had too few bp before the first indel"
	    << "\n\t" << end_match_window << " did not have the maximal number of end matches within the specified window"
	    << "\n\t" << num_end_matches  << " had too few bp matches along the ends"
	    << "\n"   << region_alignments.size() << " PASSED ALL FILTERS" << "\n" << std::endl; 
  
  // Output the spanning reads to a BAM file, if requested
  if (bam_writer.IsOpen()){
    for (auto read_iter = region_alignments.begin(); read_iter != region_alignments.end(); read_iter++){
      // Add RG to BAM record based on file
      std::string rg_tag = "lobSTR;" + file_read_groups[read_iter->Filename] + ";" + file_read_groups[read_iter->Filename];
      read_iter->AddTag("RG", "Z", rg_tag);
      
      // Add STR start and stop tags
      bool success;
      if (read_iter->HasTag("XS"))
	read_iter->RemoveTag("XS");
      if(!read_iter->AddTag("XS",  "I", region_iter->start()))
	printErrorAndDie("Failed to modify XS tag");
      if (read_iter->HasTag("XE"))
	read_iter->RemoveTag("XE");
      if(!read_iter->EditTag("XE", "I", region_iter->stop()))
	printErrorAndDie("Failed to modify XE tag");
      if (!bam_writer.SaveAlignment(*read_iter))  
	printErrorAndDie("Failed to save alignment for STR-spanning read");
    }
  }

  // Separate the reads based on their associated read groups
  std::map<std::string, int> rg_indices;
  for (auto align_iter = region_alignments.begin(); align_iter != region_alignments.end(); align_iter++){
    std::string rg = file_read_groups[align_iter->Filename];
    int rg_index;
    auto index_iter = rg_indices.find(rg);
    if (index_iter == rg_indices.end()){
      rg_index = rg_indices.size(); 
      rg_indices[rg] = rg_index;
      rg_names.push_back(rg);
      alignments_by_rg.push_back(std::vector<BamTools::BamAlignment>());
    }
    else
      rg_index = index_iter->second;
    alignments_by_rg[rg_index].push_back(*align_iter);
  }
}

void BamProcessor::process_regions(BamTools::BamMultiReader& reader, 
				   std::string& region_file, std::string& fasta_dir,
				   std::map<std::string, std::string>& file_read_groups,
				   BamTools::BamWriter& bam_writer, std::ostream& out){
  std::vector<Region> regions;
  readRegions(region_file, regions);
  orderRegions(regions);
  
  // Data structures for accessing proximal SNP haplotypes
  //std::vector<SNPTree*> snp_trees;
  //std::map<std::string, unsigned int> sample_indices;

  std::string ref_seq;
  BamTools::RefVector ref_vector = reader.GetReferenceData();
  int32_t str_start, str_stop;
  int cur_chrom_id = -1; std::string chrom_seq;
  for (auto region_iter = regions.begin(); region_iter != regions.end(); region_iter++){
    std::cerr << "Processing region " << region_iter->chrom() << " " << region_iter->start() << " " << region_iter->stop() << std::endl;
    int chrom_id = reader.GetReferenceID(region_iter->chrom());
    
    // Read FASTA sequence for chromosome 
    if (cur_chrom_id != chrom_id){
      cur_chrom_id      = chrom_id;
      std::string chrom = ref_vector[cur_chrom_id].RefName;
      std::cerr << "Reading fasta file for " << chrom << std::endl;
      readFasta(chrom+".fa", fasta_dir, chrom_seq);
    }

    // Construct SNP trees for heterozygous SNPs around the current region
    /*
    if (have_vcf){
      destroy_snp_trees(snp_trees);
      snp_trees.clear();
      sample_indices.clear();
      create_snp_trees(region_iter->chrom(), (region_iter->start() > MAX_MATE_DIST ? region_iter->start()-MAX_MATE_DIST : 1), 
		       region_iter->stop()+MAX_MATE_DIST, phased_vcf, sample_indices, snp_trees);
    }
    */

    if(!reader.SetRegion(chrom_id, region_iter->start(), chrom_id, region_iter->stop()))
      printErrorAndDie("One or more BAM files failed to set the region properly");

    std::vector<std::string> rg_names;
    std::vector< std::vector<BamTools::BamAlignment> > alignments_by_rg;
    read_and_filter_reads(reader, chrom_seq, region_iter, file_read_groups, rg_names, alignments_by_rg, bam_writer);

    process_reads(alignments_by_rg, rg_names, *region_iter, out);
  }
  //destroy_snp_trees(snp_trees); snp_trees.clear(); // Delete remaining trees
}



void BamProcessor::process_reads(std::vector< std::vector<BamTools::BamAlignment> >& alignments_by_rg, 
				 std::vector<std::string>& rg_names, Region& region, std::ostream& out){
  
}
