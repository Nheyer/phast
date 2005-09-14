/* $Id: subst_distrib.c,v 1.23 2005-09-14 18:38:32 acs Exp $ 
   Written by Adam Siepel, 2005
   Copyright 2005, Adam Siepel, University of California 
*/

/* distributions of numbers of substitutions, prior and posterior */

#include <subst_distrib.h>
#include <misc.h>
#include <sufficient_stats.h>
#include <prob_vector.h>
#include <prob_matrix.h>

/* (used below) compute and return a set of matrices giving p(b, n |
   j), the probability of n substitutions and a final base b given j
   jumps, for all n, j such that 0 <= n,j <= jmax.  If 'condition_on'
   has a nonnegative value, then instead the distribution p(b, n | j,
   a), conditional on a starting base a (whose value is given by
   'condition_on'), will be computed.  The return value A will be such
   that A[b]->data[n][j] = p(b, n | j) [or p(b, n | j, a)] */
Matrix **get_substs_and_bases_given_jumps(JumpProcess *jp, int jmax, 
                                          int condition_on) {
  int i, j, k, n;
  int size = jp->R->nrows;
  Matrix **A = smalloc(size * sizeof(void*));

  for (i = 0; i < size; i++) {
    A[i] = mat_new(jmax, jmax);
    mat_zero(A[i]);
  }

  /* initialization */
  if (condition_on < 0) 
    for (i = 0; i < size; i++)
      A[i]->data[0][0] = jp->mod->backgd_freqs->data[i];
  else 
    A[condition_on]->data[0][0] = 1;

  /* recurrence */
  for (j = 1; j < jmax; j++) {
    for (n = 0; n <= j; n++) {
      for (i = 0; i < size; i++) {
        A[i]->data[n][j] = A[i]->data[n][j-1] * jp->R->data[i][i];
        if (n > 0) 
          for (k = 0; k < size; k++) {
            if (k == i) continue;
            A[i]->data[n][j] += A[k]->data[n-1][j-1] * jp->R->data[k][i];
          }
      }
    }
  }

  return A;
}

/* define jump process based on substitution model */
JumpProcess *sub_define_jump_process(TreeModel *mod) {
  JumpProcess *jp = smalloc(sizeof(JumpProcess));
  int i, j, n, size = mod->rate_matrix->size;
  double totlen = tr_total_len(mod->tree);
  jp->njumps_max = max(20, 15 * totlen);
  jp->R = mat_new(size, size);
  jp->lambda = 0;
  jp->mod = mod;

  /* set lambda to max_a -q_aa */
  for (j = 0; j < size; j++) {
    double val = -mm_get(mod->rate_matrix, j, j);
    if (val > jp->lambda) jp->lambda = val;
  }

  /* now define jump matrix R */
  for (i = 0; i < size; i++) {
    for (j = 0; j < size; j++) {
      jp->R->data[i][j] = mm_get(mod->rate_matrix, i, j) / jp->lambda;
      if (i == j) jp->R->data[i][j] += 1;
    }
  }

  jp->A = get_substs_and_bases_given_jumps(jp, jp->njumps_max, -1);
  /* A[i]->data[n][j] = p(i, n | j) */

  jp->B = smalloc(size * sizeof(void*));

  for (i = 0; i < size; i++)
    jp->B[i] = get_substs_and_bases_given_jumps(jp, jp->njumps_max, i);
  /* jp->B[i][k]->data[n][j] is p(k, n | j, i), the prob. of n subst. and
     final base k given j jumps and starting base i */

  /* also precompute jp->M, a marginalized version of jp->A.
     jp->M->data[n][j] is p(n | j), the probability of n subst given j
     jumps */
  jp->M = mat_new(jp->njumps_max, jp->njumps_max);
  mat_zero(jp->M);
  for (n = 0; n < jp->njumps_max; n++) 
    for (j = 0; j < jp->njumps_max; j++) 
      for (i = 0; i < size; i++) 
        jp->M->data[n][j] += jp->A[i]->data[n][j];
  /* i.e., p(n | j) += p(i, n | j) */

  /* finally, precompute conditional distributions for each branch */
  jp->branch_distrib = smalloc(mod->tree->nnodes * sizeof(void*));
  for (i = 0; i < mod->tree->nnodes; i++) {
    TreeNode *n = lst_get_ptr(mod->tree->nodes, i);
    if (n == mod->tree) 
      jp->branch_distrib[n->id] = NULL;
    else
      jp->branch_distrib[n->id] = 
        sub_distrib_branch_conditional(jp, n->dparent);
  }

  return jp;
}

void sub_free_jump_process(JumpProcess *jp) {
  int i, j;
  for (i = 0; i < jp->R->nrows; i++)
    mat_free(jp->A[i]);
  free(jp->A);
  for (j = 0; j < jp->R->nrows; j++) {
    for (i = 0; i < jp->R->nrows; i++) 
      mat_free(jp->B[j][i]);
    free(jp->B[j]);
  }
  free(jp->B);
  for (i = 0; i < jp->mod->tree->nnodes; i++) {
    if (jp->branch_distrib[i] != NULL)
      for (j = 0; j < jp->R->nrows; j++)
        mat_free(jp->branch_distrib[i][j]);
  }
  free(jp->branch_distrib);
  mat_free(jp->R);
  mat_free(jp->M);
  free(jp);
}

/* compute and return a probability vector giving p(n | t), the probability
   of n substitutions given a branch of length t */
Vector *sub_distrib_branch(JumpProcess *jp, double t) {
  int n, j;
  Vector *pois = pv_poisson(jp->lambda * t);  
  Vector *distrib = vec_new(pois->size);

  assert(jp->njumps_max > pois->size);

  /* combine jp->M with Poisson to get desired distribution */
  vec_zero(distrib);
  for (n = 0; n < pois->size; n++) 
    for (j = 0; j < pois->size; j++) 
      distrib->data[n] += jp->M->data[n][j] * pois->data[j];

  vec_free(pois);

  pv_normalize(distrib);
  return distrib;
}

/* compute and return an array of matrices giving p(b, n | a, t), the
   probability of n substitutions and final base b given starting base
   a and branch length t, for all a, b, and n.  The return value D
   will be such that D[a]->data[b][n] = p(b, n | a, t) */
Matrix **sub_distrib_branch_conditional(JumpProcess *jp, double t) {
  int i, j, n, k;
  Vector *pois = pv_poisson(jp->lambda * t);  
  int size = jp->mod->rate_matrix->size;
  Matrix **D = smalloc(size * sizeof(void*));

  assert(jp->njumps_max > pois->size);

  for (i = 0; i < size; i++) {
    D[i] = mat_new(size, pois->size);
    mat_zero(D[i]);
  }

  /* recall that jp->B[k][i]->data[n][j] is p(i, n | j, k), the
     prob. of n subst. and final base i given j jumps and starting
     base k */

  /* now combine with Poisson to get desired distribution */
  for (k = 0; k < size; k++)
    for (n = 0; n < pois->size; n++) 
      for (j = 0; j < pois->size; j++) 
        for (i = 0; i < size; i++)
          D[k]->data[i][n] += jp->B[k][i]->data[n][j] * pois->data[j];
  /* i.e., p(i, n | k, t) += p(i, n | j, k, t) * p(j | t) */

  vec_free(pois);

  for (i = 0; i < size; i++) 
    pm_normalize(D[i]);

  return D;
}

/* compute and return a probability vector giving the (prior)
   distribution over the number of substitutions for a given tree
   model */
Vector *sub_prior_distrib_site(JumpProcess *jp) {
  return sub_distrib_branch(jp, tr_total_len(jp->mod->tree));
}

/* compute and return a probability vector giving the posterior
   distribution over the number of substitutions per site given a tree
   model and alignment column */
Vector *sub_posterior_distrib_site(JumpProcess *jp, MSA *msa, int tuple_idx) {
  int lidx, n, i, j, k, a, b, c;
  Matrix **L = smalloc(jp->mod->tree->nnodes * sizeof(void*));
  List *traversal = tr_postorder(jp->mod->tree);
  int size = jp->mod->rate_matrix->size;
  Vector *retval;
  int *maxsubst = smalloc(jp->mod->tree->nnodes * sizeof(int));

  assert(jp->mod->order == 0);
  assert(msa->ss != NULL);

  if (jp->mod->msa_seq_idx == NULL)
    tm_build_seq_idx(jp->mod, msa);

  for (lidx = 0; lidx < lst_size(traversal); lidx++) {
    TreeNode *node = lst_get_ptr(traversal, lidx);

    L[node->id] = mat_new(size, 500);
    mat_zero(L[node->id]);
    /* L[node->id]->[a][n] is the joint probability of n substitutions
       and the data beneath node, given that node has label a */

    if (node->lchild == NULL) {    /* leaf -- base case */
      char c = ss_get_char_tuple(msa, tuple_idx, 
                                 jp->mod->msa_seq_idx[node->id], 0);
      if (msa->is_missing[(int)c] || c == GAP_CHAR)
        for (a = 0; a < size; a++)
          L[node->id]->data[a][0] = 1;
      else {
        if (msa->inv_alphabet[(int)c] < 0)
          die("ERROR: bad character in alignment ('%c')\n", c);
        L[node->id]->data[msa->inv_alphabet[(int)c]][0] = 1;
      }

      maxsubst[node->id] = 0;	/* max no. subst. beneath node */
    }
    
    else {            /* internal node -- recursive case */

      Matrix **d_left = jp->branch_distrib[node->lchild->id];
      Matrix **d_right = jp->branch_distrib[node->rchild->id];

      maxsubst[node->id] = max(maxsubst[node->lchild->id] + d_left[0]->ncols - 1, 
                               maxsubst[node->rchild->id] + d_right[0]->ncols - 1);

      for (n = 0; n <= maxsubst[node->id]; n++) {
        for (j = 0; j <= n; j++) {
          int min_i, max_i, min_k, max_k;
          min_i = max(0, j - d_left[0]->ncols + 1);
          max_i = min(j, maxsubst[node->lchild->id]);
          min_k = max(0, n - j - d_right[0]->ncols + 1);
          max_k = min(n - j, maxsubst[node->rchild->id]);

          for (a = 0; a < size; a++) {
            double left = 0, right = 0;
	    
            for (b = 0; b < size; b++) 
              /* i goes from 0 to j, but we can trim off extreme vals */
              for (i = min_i; i <= max_i; i++) 
                left += L[node->lchild->id]->data[b][i] * 
                  d_left[a]->data[b][j-i];

            for (c = 0; c < size; c++) 
              /* k goes from 0 to n-j, but we can trim off extreme vals */
              for (k = min_k; k <= max_k; k++) 
                right += L[node->rchild->id]->data[c][k] * 
                  d_right[a]->data[c][n-j-k];
      
            L[node->id]->data[a][n] += (left * right);
          }
        }
      }
    }
  }

  retval = vec_new(maxsubst[jp->mod->tree->id] + 1);
  vec_zero(retval);
  for (n = 0; n <= maxsubst[jp->mod->tree->id]; n++)
    for (a = 0; a < size; a++)
      retval->data[n] += L[jp->mod->tree->id]->data[a][n] * 
        jp->mod->backgd_freqs->data[a];

  normalize_probs(retval->data, retval->size);

  /* trim off very small values */
  for (n = maxsubst[jp->mod->tree->id]; n >= 0 && retval->data[n] < 1e-10; n--);
  retval->size = n+1;

  for (lidx = 0; lidx < jp->mod->tree->nnodes; lidx++)
    mat_free(L[lidx]);
  free(L);
  free(maxsubst);

  pv_normalize(retval);
  return retval;
}

/* compute and return a probability vector giving the (prior)
   distribution over the number of substitutions for a given tree
   model and number of sites */
Vector *sub_prior_distrib_alignment(JumpProcess *jp, int nsites) {
  Vector *p = sub_prior_distrib_site(jp);
  Vector *retval = pv_convolve(p, nsites);
  vec_free(p);
  return retval;
}

/* compute and return a probability vector giving the posterior
   distribution over the number of substitutions for a given 
   model and alignment */
Vector *sub_posterior_distrib_alignment(JumpProcess *jp, MSA *msa) {
  int tup;
  Vector *retval;
  Vector **tup_p = smalloc(msa->ss->ntuples * sizeof(void*));
  int *counts = smalloc(msa->ss->ntuples * sizeof(int));

  for (tup = 0; tup < msa->ss->ntuples; tup++) {
    tup_p[tup] = sub_posterior_distrib_site(jp, msa, tup); 
    counts[tup] = msa->ss->counts[tup]; /* have to convert to int */
  }

  retval = pv_convolve_many(tup_p, counts, msa->ss->ntuples);

  for (tup = 0; tup < msa->ss->ntuples; tup++) 
    vec_free(tup_p[tup]);
  free(tup_p);
  free(counts);
  
  return retval;
}

/* compute mean and variance of number of substitutions, given tree
   model and alignment.  These can be obtained without doing the
   convolution */
void sub_posterior_stats_alignment(JumpProcess *jp, MSA *msa, 
                                   double *mean, double *variance) {
  int tup;
  double this_mean, this_var;
  Vector *p;
  *mean = 0; *variance = 0;
  for (tup = 0; tup < msa->ss->ntuples; tup++) {
    p = sub_posterior_distrib_site(jp, msa, tup); 
    pv_stats(p, &this_mean, &this_var);
    *mean += this_mean * msa->ss->counts[tup];
    *variance += this_var * msa->ss->counts[tup];
    vec_free(p);
  }
}


/* compute and return a matrix giving the *joint* distribution of the
   number of substitutions in the left and in the right subtree
   beneath the root.  Assumes branch length of 0 to right.  (Reroot
   tree with tr_reroot to get joint for any subtree and supertree.)
   If msa != NULL, the posterior distribution for tuple 'tuple_idx' is
   computed, otherwise the prior distribution is computed.  The
   returned matrix is such that retval->data[n1][n2] is the posterior
   probability of n1 substitutions in the left subtree and n2
   substitutions in the right subtree  */
Matrix *sub_joint_distrib_site(JumpProcess *jp, MSA *msa, int tuple_idx) {
  int lidx, n, i, j, k, a, b, c, n1, n2, n1_max = 0, n2_max = 0, done;
  Matrix **L = smalloc(jp->mod->tree->nnodes * sizeof(void*));
  List *traversal = tr_postorder(jp->mod->tree);
  int size = jp->mod->rate_matrix->size;
  Matrix *retval;
  Matrix **d_left, **d_right;
  double sum;
  int *maxsubst = smalloc(jp->mod->tree->nnodes * sizeof(int));

  assert(jp->mod->order == 0);

  if (msa != NULL && jp->mod->msa_seq_idx == NULL)
    tm_build_seq_idx(jp->mod, msa);

  for (lidx = 0; lidx < lst_size(traversal); lidx++) {
    TreeNode *node = lst_get_ptr(traversal, lidx);

    L[node->id] = mat_new(size, 500);
    mat_zero(L[node->id]);
    /* L[node->id]->[a][n] is the joint probability of n substitutions
       and the data beneath node, given that node has label a */

    if (node->lchild == NULL) {    /* leaf -- base case */
      char c;
      if (msa != NULL)
        c = ss_get_char_tuple(msa, tuple_idx, jp->mod->msa_seq_idx[node->id], 0);
      if (msa == NULL || msa->is_missing[(int)c] || c == GAP_CHAR)
        for (a = 0; a < size; a++)
          L[node->id]->data[a][0] = 1;
      else {
        assert(msa->inv_alphabet[(int)c] >= 0);
        L[node->id]->data[msa->inv_alphabet[(int)c]][0] = 1;
      }

      maxsubst[node->id] = 0;	/* max no. subst. beneath node */
    }
    
    else {            /* internal node -- recursive case */

      d_left = jp->branch_distrib[node->lchild->id];
      d_right = jp->branch_distrib[node->rchild->id];

      maxsubst[node->id] = max(maxsubst[node->lchild->id] + d_left[0]->ncols - 1, 
                               maxsubst[node->rchild->id] + d_right[0]->ncols - 1);
      
      if (node == jp->mod->tree) {
        /* save these for below */
        n1_max = maxsubst[jp->mod->tree->lchild->id] + d_left[0]->ncols;
        n2_max = maxsubst[jp->mod->tree->rchild->id] + d_right[0]->ncols;
      }

      for (n = 0; n <= maxsubst[node->id]; n++) {
        for (j = 0; j <= n; j++) {	
          int min_i, max_i, min_k, max_k;
          min_i = max(0, j - d_left[0]->ncols + 1);
          max_i = min(j, maxsubst[node->lchild->id]);
          min_k = max(0, n - j - d_right[0]->ncols + 1);
          max_k = min(n - j, maxsubst[node->rchild->id]);

          for (a = 0; a < size; a++) {
            double left = 0, right = 0;

            for (b = 0; b < size; b++) 
              for (i = min_i; i <= max_i; i++) 
                left += L[node->lchild->id]->data[b][i] * 
                  d_left[a]->data[b][j-i];

            for (c = 0; c < size; c++) 
              for (k = min_k; k <= max_k; k++) 
                right += L[node->rchild->id]->data[c][k] * 
                  d_right[a]->data[c][n-j-k];
      
            L[node->id]->data[a][n] += (left * right);
          }
        }
      }
    }
  }

  retval = mat_new(n1_max, n2_max);
  mat_zero(retval);
  d_left = jp->branch_distrib[jp->mod->tree->lchild->id];
  sum = 0;
  for (n1 = 0; n1 < n1_max; n1++) {
    for (n2 = 0; n2 < n2_max; n2++) {
      for (a = 0; a < size; a++) {
        double left = 0;
        int min_i = max(0, n1 - d_left[a]->ncols + 1);
        for (b = 0; b < size; b++) 
          for (i = min_i; i <= n1; i++) 
            left += L[jp->mod->tree->lchild->id]->data[b][i] * 
              d_left[a]->data[b][n1-i];
        retval->data[n1][n2] += left * jp->mod->backgd_freqs->data[a] * 
          L[jp->mod->tree->rchild->id]->data[a][n2];
      }
      sum += retval->data[n1][n2];
    }
  }
  mat_scale(retval, 1/sum);     /* normalize */

  /* trim off very small values */
  done = FALSE;
  for (n1 = n1_max-1; !done && n1 >= 0; n1--) 
    for (n2 = 0; !done && n2 < n2_max; n2++) 
      if (retval->data[n1][n2] >= 1e-10) {
        n1_max = n1+1;
        done = TRUE;
      }
  done = FALSE;
  for (n2 = n2_max-1; !done && n2 >= 0; n2--) 
    for (n1 = 0; !done && n1 < n1_max; n1++) 
      if (retval->data[n1][n2] >= 1e-10) {
        n2_max = n2+1;
        done = TRUE;
      }
  mat_resize(retval, n1_max, n2_max);

  for (lidx = 0; lidx < jp->mod->tree->nnodes; lidx++)
    mat_free(L[lidx]);
  free(L);

  free(maxsubst);

  pm_normalize(retval);
  return retval;
}

/* compute and return a probability matrix giving the (prior) joint
   distribution over the number of substitutions in the left and in
   the right subtree beneath the root, assuming the given tree model
   and number of sites.  As above, designed for use with tr_reroot.
   The returned matrix is such that retval->data[x][y] is the
   posterior probability of x substitutions in the left subtree and y
   substitutions in the right subtree */
Matrix *sub_prior_joint_distrib_alignment(JumpProcess *jp, int nsites) {
  Matrix *p = sub_joint_distrib_site(jp, NULL, -1);
  Matrix *retval = pm_convolve_fast(p, nsites);
  mat_free(p);
  return retval;
}

/* compute and return a probability matrix giving the posterior joint
   distribution over the number of substitutions in the left and in
   the right subtree beneath the root, given a tree model and an
   alignment.  As above, designed for use with tr_reroot.  The
   returned matrix is such that retval->data[x][y] is the posterior
   probability of x substitutions in the left subtree and y
   substitutions in the right subtree */
Matrix *sub_posterior_joint_distrib_alignment(JumpProcess *jp, MSA *msa) {
  int tup;
  Matrix *retval;
  Matrix **tup_p = smalloc(msa->ss->ntuples * sizeof(void*));
  int *counts = smalloc(msa->ss->ntuples * sizeof(int));
  
  for (tup = 0; tup < msa->ss->ntuples; tup++) {
    tup_p[tup] = sub_joint_distrib_site(jp, msa, tup); 
    counts[tup] = msa->ss->counts[tup]; /* have to convert to int */
  }

  retval = pm_convolve_many(tup_p, counts, msa->ss->ntuples);

  for (tup = 0; tup < msa->ss->ntuples; tup++) 
    mat_free(tup_p[tup]);
  free(tup_p);
  free(counts);

  return retval;
}

/* compute mean and (marginal) variance of number of substitutions in
   left and right subtree, given tree model and alignment.  These can
   be obtained without doing the convolution.  As above, designed for
   use with tr_reroot.  */
void sub_posterior_joint_stats_alignment(JumpProcess *jp, MSA *msa, 
                                         double *mean_tot, double *var_tot,
                                         double *mean_left, double *var_left,
                                         double *mean_right, double *var_right) {
  int tup;
  Matrix *p;
  double this_mean, this_var;
  Vector *marg_x, *marg_y, *marg_tot;
  *mean_left = *var_left = *mean_right = *var_right = *mean_tot = *var_tot = 0;
  for (tup = 0; tup < msa->ss->ntuples; tup++) {
    p = sub_joint_distrib_site(jp, msa, tup); 
    marg_x = pm_marg_x(p);
    pv_stats(marg_x, &this_mean, &this_var);
    *mean_left += this_mean * msa->ss->counts[tup];
    *var_left += this_var * msa->ss->counts[tup];
    marg_y = pm_marg_y(p);
    pv_stats(marg_y, &this_mean, &this_var);
    *mean_right += this_mean * msa->ss->counts[tup];
    *var_right += this_var * msa->ss->counts[tup];
    marg_tot = pm_marg_tot(p);
    pv_stats(marg_tot, &this_mean, &this_var);
    *mean_tot += this_mean * msa->ss->counts[tup];
    *var_tot += this_var * msa->ss->counts[tup];
    vec_free(marg_x);
    vec_free(marg_y);
    vec_free(marg_tot);
    mat_free(p);
  }
}

/* compute p-values and related stats for a given alignment and model
   and each of a set of features.  Returns an array of p_value_stats
   objects, one for each feature (dimension
   lst_size(feat->features)) */   
p_value_stats *sub_p_value_many(JumpProcess *jp, MSA *msa, List *feats, 
                                double ci /* confidence interval; if
                                             -1, posterior mean will
                                             be used */
                                ) {

  Vector *p, *prior;
  int maxlen = -1, len, idx, i, j, logmaxlen, loglen, checksum;
  GFF_Feature *f;
  double *post_mean, *post_var;
  double this_min, this_max;
  p_value_stats *stats = smalloc(lst_size(feats) * 
                                 sizeof(p_value_stats));
  char *used = smalloc(msa->ss->ntuples * sizeof(char));
  Vector **pow_p, **pows;

  /* find max length of feature.  Simultaneously, figure out which
     column tuples actually used (saves time below) */
  for (i = 0; i < msa->ss->ntuples; i++) used[i] = 'N';
  for (idx = 0; idx < lst_size(feats); idx++) {
    f = lst_get_ptr(feats, idx);
    len = f->end - f->start + 1;
    if (len > maxlen) maxlen = len;
    for (i = f->start - 1; i < f->end; i++)
      if (used[msa->ss->tuple_idx[i]] == 'N')
        used[msa->ss->tuple_idx[i]] = 'Y';
  }

  /* compute "powers" of prior distribution, to allow fast computation
     of convolution of prior for any feature length */
  logmaxlen = log2_int(maxlen);
  pow_p = smalloc((logmaxlen+1) * sizeof(void*));
  pow_p[0] = sub_prior_distrib_site(jp);
  for (i = 1; i <= logmaxlen; i++) 
    pow_p[i] = pv_convolve(pow_p[i-1], 2);
  pows = smalloc((logmaxlen+1) * sizeof(void*)); /* for use below */

  /* compute mean and variance of posterior for all column tuples */
  post_mean = smalloc(msa->ss->ntuples * sizeof(double));
  post_var = smalloc(msa->ss->ntuples * sizeof(double));
  for (idx = 0; idx < msa->ss->ntuples; idx++) {
    if (used[idx] == 'N') continue; /* can save fairly expensive call below */
    p = sub_posterior_distrib_site(jp, msa, idx); 
    pv_stats(p, &post_mean[idx], &post_var[idx]);
    vec_free(p);
  }

  /* now obtain stats for each feature */
  for (idx = 0; idx < lst_size(feats); idx++) {
    f = lst_get_ptr(feats, idx);
    len = f->end - f->start + 1;
    loglen = log2_int(len);

    /* compute convolution of prior from powers */
    j = checksum = 0;
    for (i = 0; i <= loglen; i++) {
      unsigned bit_i = (len >> i) & 1;
      if (bit_i) {
        pows[j++] = pow_p[i];
        checksum += int_pow(2, i);
      }
    }
    assert(checksum == len);
    prior = pv_convolve_many(pows, NULL, j);

    pv_stats(prior, &stats[idx].prior_mean, &stats[idx].prior_var);
    pv_confidence_interval(prior, 0.95, &stats[idx].prior_min, 
                           &stats[idx].prior_max);


    stats[idx].post_mean = stats[idx].post_var = 0;
    for (i = f->start - 1; i < f->end; i++) {
      stats[idx].post_mean += post_mean[msa->ss->tuple_idx[i]];
      stats[idx].post_var += post_var[msa->ss->tuple_idx[i]];
    }
    
    if (ci != -1)
      norm_confidence_interval(stats[idx].post_mean, sqrt(stats[idx].post_var), 
                               ci, &this_min, &this_max);
    else 
      this_min = this_max = stats[idx].post_mean;

    stats[idx].post_min = floor(this_min);
    stats[idx].post_max = ceil(this_max);

    stats[idx].p_cons = pv_p_value(prior, stats[idx].post_max, LOWER);
    stats[idx].p_anti_cons = pv_p_value(prior, stats[idx].post_min, UPPER);    

    vec_free(prior);
  }

  for (idx = 0; idx <= logmaxlen; idx++)
    vec_free(pow_p[idx]);
  free(pow_p);
  free(pows);

  free(post_mean);
  free(post_var);
  free(used);

  return stats;
}

/* (used by sub_p_value_joint_many) compute maximum length of element
   for which to do explicit convolution, based on given means and
   standard devs for subtrees, and based on max_convolve_size */
int max_convolve_len(int max_convolve_size, double mean_l, double sd_l, 
                     double mean_r, double sd_r) {
  double maxsize;

  int l = sqrt(max_convolve_size / 
               ((mean_l + 6 * sd_l) * (mean_r + 6 * sd_r)));
  /* (lower bound on max, obtained by replacing sqrt(l) with l) */

  /* can solve exactly for max, but you have to work with a messy
     polynomial; easier just to solve by trial and error */
  do {
    l++;
    maxsize = (l * mean_l + 6 * sd_l * sqrt(l)) *
      (l * mean_r + 6 * sd_r * sqrt(l));
    /* bound on size of matrix for given length, using CLT approx  */
  } while (maxsize < max_convolve_size);

  return l-1;
}

/* left/right subtree version of above: compute p-values and related
   stats for a given alignment and model and each of a set of
   features.  Returns an array of p_value_joint_stats objects, one for
   each feature (dimension lst_size(feat->features)).  Tree model is
   assumed to have already been rerooted by tr_reroot */   
p_value_joint_stats*
sub_p_value_joint_many(JumpProcess *jp, MSA *msa, List *feats, 
                       double ci, /* confidence interval; if
                                     -1, posterior mean will
                                     be used */
                       int max_convolve_size, 
                                /* maximum matrix size (rows*cols) for
                                   exact computation of prior
                                   convolution; beyond this size, an
                                   approximation is used  */
                       FILE *timing_f /* log file for timing info */
                       ) {

  Matrix *p, *prior, *prior_site;
  int maxlen = -1, len, idx, i, j, logmaxlen, loglen, max_nrows, max_ncols,
    max_conv_len, checksum;
  GFF_Feature *f;
  double *post_mean_left, *post_mean_right, *post_mean_tot, *post_var_left,
    *post_var_right, *post_var_tot;
  double this_min_left, this_min_right, this_max_left, this_max_right, 
    this_min_tot, this_max_tot, prior_site_mean_left, prior_site_var_left,
    prior_site_mean_right, prior_site_var_right, sd_l, sd_r, rho;
  p_value_joint_stats *stats = smalloc(lst_size(feats) * 
                                       sizeof(p_value_joint_stats));
  Vector *prior_site_marg_left, *prior_site_marg_right,
    *prior_marg_left, *prior_marg_right, *marg, *cond;
  char *used = smalloc(msa->ss->ntuples * sizeof(char));
  Matrix **pow_p, **pows;
  struct timeval marker_time;

  /* find max length of feature.  Simultaneously, figure out which
     column tuples actually used (saves time below)  */
  for (i = 0; i < msa->ss->ntuples; i++) used[i] = 'N';
  for (idx = 0; idx < lst_size(feats); idx++) {
    f = lst_get_ptr(feats, idx);
    len = f->end - f->start + 1;
    if (len > maxlen) maxlen = len;
    for (i = f->start - 1; i < f->end; i++)
      if (used[msa->ss->tuple_idx[i]] == 'N')
        used[msa->ss->tuple_idx[i]] = 'Y';
  }

  /* compute per-site prior distribution and left/right marginals */
  prior_site = sub_joint_distrib_site(jp, NULL, -1);
  pm_stats(prior_site, &prior_site_mean_left, &prior_site_mean_right,
           &prior_site_var_left, &prior_site_var_right, &rho);
  rho /= (sqrt(prior_site_var_left * prior_site_var_right));
                                /* (convert covariance to corr. coef.) */
  prior_site_marg_left = pm_marg_x(prior_site);
  prior_site_marg_right = pm_marg_y(prior_site);

  /* compute maximum length for explicit computation of joint prior
     via convolution */
  max_conv_len = 
    max_convolve_len(max_convolve_size,
                     prior_site_mean_left, sqrt(prior_site_var_left), 
                     prior_site_mean_right, sqrt(prior_site_var_right));
  if (maxlen > max_conv_len)
    maxlen = max_conv_len;

  /* compute "powers" of prior distribution, to allow fast computation
     of convolution of prior */
  logmaxlen = log2_int(maxlen);
  pow_p = smalloc((logmaxlen+1) * sizeof(void*));
  pow_p[0] = prior_site;
  for (i = 1; i <= logmaxlen; i++) {
    if (timing_f != NULL) gettimeofday(&marker_time, NULL);
    pow_p[i] = pm_convolve(pow_p[i-1], 2);
    if (timing_f != NULL) 
      fprintf(timing_f, "pow_p[%d] (%d x %d): %f sec\n", i, 
              pow_p[i]->nrows, pow_p[i]->ncols, get_elapsed_time(&marker_time));
  }
  pows = smalloc((logmaxlen+1) * sizeof(void*)); /* for use below */

  /* compute mean and variance of (marginals of) posterior for all
     column tuples */
  post_mean_left = smalloc(msa->ss->ntuples * sizeof(double));
  post_mean_right = smalloc(msa->ss->ntuples * sizeof(double));
  post_mean_tot = smalloc(msa->ss->ntuples * sizeof(double));
  post_var_left = smalloc(msa->ss->ntuples * sizeof(double));
  post_var_right = smalloc(msa->ss->ntuples * sizeof(double));
  post_var_tot = smalloc(msa->ss->ntuples * sizeof(double));
  for (idx = 0; idx < msa->ss->ntuples; idx++) {
    if (used[idx] == 'N') continue; /* can save fairly expensive call below */
    p = sub_joint_distrib_site(jp, msa, idx); 
    marg = pm_marg_x(p);
    pv_stats(marg, &post_mean_left[idx], &post_var_left[idx]);
    vec_free(marg);
    marg = pm_marg_y(p);
    pv_stats(marg, &post_mean_right[idx], &post_var_right[idx]);
    vec_free(marg);
    marg = pm_marg_tot(p);
    pv_stats(marg, &post_mean_tot[idx], &post_var_tot[idx]);
    vec_free(marg);
    mat_free(p);
  }

  /* now obtain stats for each feature */
  for (idx = 0; idx < lst_size(feats); idx++) {
    f = lst_get_ptr(feats, idx);
    len = f->end - f->start + 1;
    loglen = log2_int(len);

    if (len <= max_conv_len) {
      /* compute convolution of prior from powers */
      j = checksum = 0;
      for (i = 0; i <= loglen; i++) {
        unsigned bit_i = (len >> i) & 1;
        if (bit_i) {
          pows[j++] = pow_p[i];
          checksum += int_pow(2, i);
        }
      }
      assert(checksum == len);

      if (len > 25) {
        /* use central limit theorem to limit size of matrix to keep
           track of */
        max_nrows = ceil(len * prior_site_mean_left + 
                         6 * sqrt(len * prior_site_var_left));
        max_ncols = ceil(len * prior_site_mean_right + 
                         6 * sqrt(len * prior_site_var_right));
      }
      else {
        max_nrows = pow_p[0]->nrows * len;
        max_ncols = pow_p[0]->ncols * len;
      }

      if (timing_f != NULL) gettimeofday(&marker_time, NULL);
      prior = pm_convolve_many_fast(pows, j, max_nrows, max_ncols);
      if (timing_f != NULL)
        fprintf(timing_f, "len = %d (%d x %d): %f sec\n", len, max_nrows, 
                max_ncols, get_elapsed_time(&marker_time));

      prior_marg_left = pm_marg_x(prior);
      prior_marg_right = pm_marg_y(prior);
    }
    else {
      prior = NULL;             /* won't be used explicitly */
      prior_marg_left = pv_convolve(prior_site_marg_left, len);
      prior_marg_right = pv_convolve(prior_site_marg_right, len);
      if (timing_f != NULL)
        fprintf(timing_f, "len = %d (%d x %d): [skipping joint convolution]\n",
                len, max_nrows, max_ncols);
    }

    pv_stats(prior_marg_left, &stats[idx].prior_mean_left, 
             &stats[idx].prior_var_left);
    pv_confidence_interval(prior_marg_left, 0.95, &stats[idx].prior_min_left,
                           &stats[idx].prior_max_left);
    pv_stats(prior_marg_right, &stats[idx].prior_mean_right, 
             &stats[idx].prior_var_right);
    pv_confidence_interval(prior_marg_right, 0.95, &stats[idx].prior_min_right,
                           &stats[idx].prior_max_right);

    stats[idx].post_mean_left = stats[idx].post_mean_right = 
      stats[idx].post_var_left = stats[idx].post_var_right = 
      stats[idx].post_mean_tot = stats[idx].post_var_tot = 0;
    for (i = f->start - 1; i < f->end; i++) {
      stats[idx].post_mean_left += post_mean_left[msa->ss->tuple_idx[i]];
      stats[idx].post_mean_right += post_mean_right[msa->ss->tuple_idx[i]];
      stats[idx].post_mean_tot += post_mean_tot[msa->ss->tuple_idx[i]];
      stats[idx].post_var_left += post_var_left[msa->ss->tuple_idx[i]];
      stats[idx].post_var_right += post_var_right[msa->ss->tuple_idx[i]];
      stats[idx].post_var_tot += post_var_tot[msa->ss->tuple_idx[i]];
    }
    
    if (ci != -1) {
      norm_confidence_interval(stats[idx].post_mean_left, 
                               sqrt(stats[idx].post_var_left), 
                               ci, &this_min_left, &this_max_left);
      norm_confidence_interval(stats[idx].post_mean_right, 
                               sqrt(stats[idx].post_var_right), 
                               ci, &this_min_right, &this_max_right);
      norm_confidence_interval(stats[idx].post_mean_tot, 
                               sqrt(stats[idx].post_var_tot), 
                               ci, &this_min_tot, &this_max_tot);
    }
    else {
      this_min_left = this_max_left = stats[idx].post_mean_left;
      this_min_right = this_max_right = stats[idx].post_mean_right;
      this_min_tot = this_max_tot = stats[idx].post_mean_tot;
    }

    stats[idx].post_min_left = floor(this_min_left);
    stats[idx].post_max_left = ceil(this_max_left);
    stats[idx].post_min_right = floor(this_min_right);
    stats[idx].post_max_right = ceil(this_max_right);
    stats[idx].post_min_tot = floor(this_min_tot);
    stats[idx].post_max_tot = ceil(this_max_tot);

    /* conditional p-values */
    sd_l = sqrt(stats[idx].prior_var_left);
    sd_r = sqrt(stats[idx].prior_var_right);
    cond = prior != NULL ? pm_x_given_tot(prior, stats[idx].post_min_tot) :
      pm_x_given_tot_indep(stats[idx].post_min_tot, prior_marg_left, prior_marg_right);
    stats[idx].cond_p_cons_left = pv_p_value(cond, stats[idx].post_max_left, 
                                             LOWER);
    vec_free(cond);

    cond = prior != NULL ? pm_x_given_tot(prior, stats[idx].post_max_tot) :
      pm_x_given_tot_indep(stats[idx].post_max_tot, prior_marg_left, prior_marg_right);
    stats[idx].cond_p_anti_cons_left = pv_p_value(cond, 
                                                  stats[idx].post_min_left, 
                                                  UPPER);
    vec_free(cond);

    cond = prior != NULL ? pm_y_given_tot(prior, stats[idx].post_min_tot) :
      pm_y_given_tot_indep(stats[idx].post_min_tot, prior_marg_left, prior_marg_right);
    stats[idx].cond_p_cons_right = pv_p_value(cond, stats[idx].post_max_right, 
                                              LOWER);
    vec_free(cond);

    cond = prior != NULL ? pm_y_given_tot(prior, stats[idx].post_max_tot) :
      pm_y_given_tot_indep(stats[idx].post_max_tot, prior_marg_left, prior_marg_right);
    stats[idx].cond_p_anti_cons_right = pv_p_value(cond, 
                                                   stats[idx].post_min_right, 
                                                   UPPER);
    vec_free(cond);

    stats[idx].cond_p_approx = (prior == NULL ? TRUE : FALSE);

    /* marginal p-values */
    stats[idx].p_cons_left = pv_p_value(prior_marg_left, 
                                        stats[idx].post_max_left, LOWER);
    stats[idx].p_anti_cons_left = pv_p_value(prior_marg_left, 
                                             stats[idx].post_min_left, UPPER);
    stats[idx].p_cons_right = pv_p_value(prior_marg_right, 
                                         stats[idx].post_max_right, LOWER);
    stats[idx].p_anti_cons_right = pv_p_value(prior_marg_right, 
                                              stats[idx].post_min_right, UPPER);

    if (prior != NULL) mat_free(prior);
    vec_free(prior_marg_left);
    vec_free(prior_marg_right);
  }

  for (idx = 0; idx <= logmaxlen; idx++)
    mat_free(pow_p[idx]);       /* this will also free prior_site */
  free(pow_p);
  free(pows);
  vec_free(prior_site_marg_left);
  vec_free(prior_site_marg_right);
  free(post_mean_left);
  free(post_mean_right);
  free(post_mean_tot);
  free(post_var_left);
  free(post_var_right);
  free(post_var_tot);
  free(used);

  return stats;
}
